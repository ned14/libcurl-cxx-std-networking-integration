/* A demonstration integration of libcurl with C++ 23 Standard Networking.

Also uses WG21 status_codes to wrap CURL error codes, probably will be
standardised into C++ 26.
*/

// Currently the only decent C++ 23 std::networking is ASIO's
#define USE_EXPERIMENTAL_STD_NETWORKING 1
// Currently the only implementation of C++ 26 status_code is this
#define USE_EXPERIMENTAL_STD_STATUS_CODES 1

#include "curl/curl.h"

#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#if USE_EXPERIMENTAL_STD_NETWORKING
#define ASIO_NO_EXTENSIONS 1
#include "asio/io_context.hpp"
#include "asio/ip/tcp.hpp"
#include "asio/steady_timer.hpp"
namespace libcurl_cxx_std_networking_integration
{
  namespace networking
  {
    using asio::io_context;
    using asio::steady_timer;
    namespace ip = asio::ip;
  }  // namespace networking
}  // namespace libcurl_cxx_std_networking_integration
#endif

#include "curl_status_codes.hpp"
namespace libcurl_cxx_std_networking_integration
{
  using curl_status_codes::curl_code;
  using curl_status_codes::curlm_code;
  using curl_status_codes::error;
}  // namespace libcurl_cxx_std_networking_integration

namespace libcurl_cxx_std_networking_integration
{
  class curl_std_networking_wrapper
  {
    mutable std::mutex _lock;
    networking::io_context _ctx;

    CURLM *_curlm{nullptr};
    struct socket_state_t : std::enable_shared_from_this<socket_state_t>
    {
      networking::ip::tcp::socket s;
      std::atomic<int> listening_state{CURL_POLL_REMOVE};
      socket_state_t(networking::io_context &ctx)
          : s(ctx)
      {
      }
    };
    using socket_state_ptr = std::shared_ptr<socket_state_t>;
    std::unordered_map<curl_socket_t, socket_state_ptr> _sockets;

    networking::steady_timer _timeoutstimer{_ctx};

    struct download_state_t
    {
      std::string url;
      std::string result;
      error err;
      char *datap{nullptr}, *dataend{nullptr};
      std::atomic<bool> done{false};

      download_state_t(std::string _url)
          : url(std::move(_url))
      {
        result.resize(CURL_MAX_READ_SIZE);
        datap = result.data();
        dataend = result.data() + result.size();
      }
      ~download_state_t() { assert(done); }

      size_t write_callback(const char *ptr, size_t bytes)
      {
        if(datap + bytes > dataend)
        {
          auto diff = datap - result.data();
          result.resize(result.size() << 1);
          datap = result.data() + diff;
          dataend = result.data() + result.size();
        }
        memcpy(datap, ptr, bytes);
        datap += bytes;
        return CURLE_OK;
      }

      std::optional<std::string> get()
      {
        if(!done)
        {
          return std::nullopt;
        }
        if(err.failure())
        {
          err.throw_exception();
        }
        return std::move(result);
      }
    };
    std::unordered_map<CURL *, download_state_t> _downloads;

    // Called by ASIO to say a read is now possible
    void _read_possible(const socket_state_ptr &which, std::error_code ec)
    {
      // If being cancelled, exit immediately
      if(ec == std::errc::operation_canceled)
      {
        return;
      }
      // Immediately rearm if still being watched by curl
      if(CURL_POLL_INOUT == which->listening_state || CURL_POLL_IN == which->listening_state)
      {
        which->s.async_wait(networking::ip::tcp::socket::wait_read, [this, sock = which](auto ec) { _read_possible(sock, ec); });
      }
      // ASIO can call this from any thread, and libcurl is single threaded
      std::unique_lock g(_lock);
      // Tell libcurl that this socket is ready to read
      int handles = 0;
      CURLM_CHECK(curl_multi_socket_action(_curlm, which->s.native_handle(), ec ? CURL_CSELECT_ERR : CURL_CSELECT_IN, &handles));
    }

    // Called by ASIO to say a write is now possible
    void _write_possible(const socket_state_ptr &which, std::error_code ec)
    {
      // If being cancelled, exit immediately
      if(ec == std::errc::operation_canceled)
      {
        return;
      }
      // Immediately rearm if still being watched by curl
      if(CURL_POLL_INOUT == which->listening_state || CURL_POLL_OUT == which->listening_state)
      {
        which->s.async_wait(networking::ip::tcp::socket::wait_write, [this, sock = which](auto ec) { _write_possible(sock, ec); });
      }
      // ASIO can call this from any thread, and libcurl is single threaded
      std::unique_lock g(_lock);
      // Tell libcurl that this socket is ready to write
      int handles = 0;
      CURLM_CHECK(curl_multi_socket_action(_curlm, which->s.native_handle(), ec ? CURL_CSELECT_ERR : CURL_CSELECT_OUT, &handles));
    }

    // Called by ASIO to say the timeout which curl asked for has expired
    void _do_timeout(std::error_code ec)
    {
      // If being cancelled, exit immediately
      if(ec == std::errc::operation_canceled)
      {
        return;
      }
      // ASIO can call this from any thread, and libcurl is single threaded
      std::unique_lock g(_lock);
      // Tell libcurl to process socket timesouts
      int handles = 0;
      CURLM_CHECK(curl_multi_socket_action(_curlm, CURL_SOCKET_TIMEOUT, 0, &handles));
    }

  public:
    using download_state_ref = std::unordered_map<CURL *, download_state_t>::iterator;
    curl_std_networking_wrapper()
    {
      _curlm = curl_multi_init();
      if(_curlm == nullptr)
      {
        throw std::runtime_error("FATAL: curl_multi_init() failed.");
      }
      CURLM_CHECK(curl_multi_setopt(
      _curlm, CURLMOPT_SOCKETFUNCTION, +[](CURL *easy, curl_socket_t s, int what, void *userp, void *socketp) -> int {
        return reinterpret_cast<curl_std_networking_wrapper *>(userp)->_curlm_socketfunction(easy, s, what, socketp);
      }));
      CURLM_CHECK(curl_multi_setopt(_curlm, CURLMOPT_SOCKETDATA, this));
      CURLM_CHECK(curl_multi_setopt(
      _curlm, CURLMOPT_TIMERFUNCTION, +[](CURLM * /*unused*/, long timeout_ms, void *userp) -> int {
        return reinterpret_cast<curl_std_networking_wrapper *>(userp)->_curlm_timerfunction(timeout_ms);
      }));
      CURLM_CHECK(curl_multi_setopt(_curlm, CURLMOPT_TIMERDATA, this));
    }
    curl_std_networking_wrapper(const curl_std_networking_wrapper &) = delete;
    curl_std_networking_wrapper(curl_std_networking_wrapper &&) = delete;
    curl_std_networking_wrapper &operator=(const curl_std_networking_wrapper &) = delete;
    curl_std_networking_wrapper &operator=(curl_std_networking_wrapper &&) = delete;
    ~curl_std_networking_wrapper()
    {
      std::unique_lock g(_lock);
      if(_curlm != nullptr)
      {
        curl_multi_cleanup(_curlm);  // cancels all outstanding i/o
        _curlm = nullptr;
      }
    }

    // Called by curl to ask ASIO to watch a socket for changes
    int _curlm_socketfunction(CURL * /*unused*/, curl_socket_t s, int what, void *socketp) noexcept
    {
      try
      {
        socket_state_ptr sock;
        if(socketp != nullptr)
        {
          sock = reinterpret_cast<socket_state_t *>(socketp)->shared_from_this();
        }
        else
        {
          auto it = _sockets.find(s);
          assert(it != _sockets.end());
          if(it == _sockets.end())
          {
            return CURLM_BAD_HANDLE;
          }
          sock = it->second;
          // Cache lookup for next time
          CURLM_CHECK(curl_multi_assign(_curlm, s, sock.get()));
        }
        sock->listening_state = what;
        if(CURL_POLL_REMOVE == what)
        {
          sock->s.cancel();  // cancel all outstanding operations with ASIO on this socket
          return CURLM_OK;
        }
        if(CURL_POLL_INOUT == what || CURL_POLL_IN == what)
        {
          sock->s.async_wait(networking::ip::tcp::socket::wait_read, [this, sock](auto ec) { _read_possible(sock, ec); });
        }
        if(CURL_POLL_INOUT == what || CURL_POLL_OUT == what)
        {
          sock->s.async_wait(networking::ip::tcp::socket::wait_write, [this, sock](auto ec) { _write_possible(sock, ec); });
        }
        return CURLM_OK;
      }
      catch(...)
      {
        return -1;
      }
    }

    // Called by curl to ask ASIO to tell it when to process timeouts
    int _curlm_timerfunction(long timeout_ms) noexcept
    {
      try
      {
        if(timeout_ms == -1)
        {
          _timeoutstimer.cancel();
          return 0;
        }
        _timeoutstimer.expires_after(std::chrono::milliseconds(timeout_ms));
        _timeoutstimer.async_wait([this](auto ec) { _do_timeout(ec); });
        return 0;
      }
      catch(...)
      {
        return -1;
      }
    }

    // Called by curl to ask ASIO to create a new socket
    curl_socket_t _curl_open_socket(curlsocktype /*unused*/, struct curl_sockaddr *address) noexcept
    {
      try
      {
        if(address->family != AF_INET && address->family != AF_INET6)
        {
          return CURL_SOCKET_BAD;
        }
        if(address->socktype != SOCK_STREAM)
        {
          return CURL_SOCKET_BAD;
        }
        auto sock = std::make_shared<socket_state_t>(_ctx);
        _sockets.emplace(sock->s.native_handle(), sock);
        return sock->s.native_handle();
      }
      catch(...)
      {
        return -1;
      }
    }

    // Called by curl to ask ASIO to destroy a socket
    curl_socket_t _curl_close_socket(curl_socket_t item) noexcept
    {
      try
      {
        auto it = _sockets.find(item);
        assert(it != _sockets.end());
        if(it != _sockets.end())
        {
          it->second->s.close();
          _sockets.erase(it);
        }
        return CURLE_OK;
      }
      catch(...)
      {
        return -1;
      }
    }

    download_state_ref begin_fetch(std::string url)
    {
      std::unique_lock g(_lock);
      auto *curlh = curl_easy_init();
      if(curlh == nullptr)
      {
        throw std::runtime_error("curl_easy_init() failed.");
      }
      auto it = _downloads.emplace(curlh, std::move(url)).first;
      // Not in a multithreaded program
      CURL_CHECK(curl_easy_setopt(curlh, CURLOPT_NOSIGNAL, 1));

      // Have curl use ASIO to create and destroy sockets
      CURL_CHECK(curl_easy_setopt(
      curlh, CURLOPT_OPENSOCKETFUNCTION, +[](void *clientp, curlsocktype purpose, struct curl_sockaddr *address) -> curl_socket_t {
        return reinterpret_cast<curl_std_networking_wrapper *>(clientp)->_curl_open_socket(purpose, address);
      }));
      CURL_CHECK(curl_easy_setopt(curlh, CURLOPT_OPENSOCKETDATA, this));
      CURL_CHECK(curl_easy_setopt(
      curlh, CURLOPT_CLOSESOCKETFUNCTION,
      +[](void *clientp, curl_socket_t item) -> curl_socket_t { return reinterpret_cast<curl_std_networking_wrapper *>(clientp)->_curl_close_socket(item); }));
      CURL_CHECK(curl_easy_setopt(curlh, CURLOPT_CLOSESOCKETDATA, this));

      // Set callback to write fetched content
      CURL_CHECK(curl_easy_setopt(
      curlh, CURLOPT_WRITEFUNCTION, +[](char *ptr, size_t size, size_t nmemb, void *userdata) -> size_t {
        return reinterpret_cast<download_state_t *>(userdata)->write_callback(ptr, nmemb);
      }));
      // unordered_map values are stable in memory
      CURL_CHECK(curl_easy_setopt(curlh, CURLOPT_WRITEDATA, &it->second));

      // Set the URL to download
      CURL_CHECK(curl_easy_setopt(curlh, CURLOPT_URL, it->second.url.c_str()));

      // Register this download with curl i/o multiplexer
      CURLM_CHECK(curl_multi_add_handle(_curlm, curlh));
      return it;
    }

    // This function can be called from many kernel threads concurrently
    std::vector<download_state_ref> pump_io()
    {
      // curl needs to be poked to register handles with ASIO
      _do_timeout({});
      // Have ASIO pump completions until no work remains.
      _ctx.run();
      // Have any of the transfers completed?
      std::unique_lock g(_lock);
      std::vector<download_state_ref> transfers_done;
      for(;;)
      {
        int remaining = 0;
        auto *msg = curl_multi_info_read(_curlm, &remaining);
        if(msg == nullptr)
        {
          // Close the handles now we're outside curl_multi_info_read
          for(auto &i : transfers_done)
          {
            CURLM_CHECK(curl_multi_remove_handle(_curlm, i->first));
            curl_easy_cleanup(i->first);
            i->second.done = true;
          }
          return transfers_done;
        }
        if(msg->msg == CURLMSG_DONE)
        {
          auto it = _downloads.find(msg->easy_handle);
          assert(it != _downloads.end());
          if(it != _downloads.end())
          {
            it->second.err = curl_code(msg->data.result);
            if(it->second.err.success())
            {
              it->second.result.resize(it->second.datap - it->second.result.data());  // resize to what was downloaded
            }
            else
            {
              it->second.result.clear();  // failed
            }
            transfers_done.push_back(it);
          }
        }
      }
    }
  };
  int main(int argc, char *argv[])
  {
    try
    {
      if(argc < 2)
      {
        std::cout << "Usage: " << argv[0] << " <URL>" << std::endl;
        return 0;
      }
      curl_std_networking_wrapper wrapper;
      std::vector<curl_std_networking_wrapper::download_state_ref> downloads;
      for(int i = 1; i < argc; i++)
      {
        std::cout << "Fetching from URL " << argv[i] << "..." << std::endl;
        downloads.push_back(wrapper.begin_fetch(argv[1]));
      }
      auto downloads_togo = downloads.size();
      for(;;)
      {
        auto transfers_done = wrapper.pump_io();
        downloads_togo -= transfers_done.size();
        if(downloads_togo == 0)
        {
          break;
        }
      }
      for(auto &i : downloads)
      {
        assert(i->second.done);
        std::cout << "\n\nFrom URL " << i->first << " fetched:\n\n" << i->second.get().value() << std::endl;
      }
      return 0;
    }
    catch(const std::exception &e)
    {
      std::cerr << "FATAL: Exception thrown '" << e.what() << "'" << std::endl;
      return 1;
    }
  }
}  // namespace libcurl_cxx_std_networking_integration

int main(int argc, char *argv[])
{
  return libcurl_cxx_std_networking_integration::main(argc, argv);
}