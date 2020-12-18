#pragma once

#if USE_EXPERIMENTAL_STD_STATUS_CODES
#include "status-code/include/error.hpp"  // for WG21 status_code
#endif
#include "curl/curl.h"

namespace curl_status_codes
{
#if USE_EXPERIMENTAL_STD_STATUS_CODES
  using namespace SYSTEM_ERROR2_NAMESPACE;
#endif

  class _curl_code_domain;
  using curl_code = status_code<_curl_code_domain>;
  class _curlm_code_domain;
  using curlm_code = status_code<_curlm_code_domain>;

  class _curl_code_domain final : public status_code_domain
  {
    using _base = status_code_domain;

  public:
    using value_type = CURLcode;

    constexpr _curl_code_domain()
        : _base("{ec75763c-5529-a9f0-5a78-feb81261fcec}")
    {
    }
    static inline constexpr const _curl_code_domain &get();

    virtual _base::string_ref name() const noexcept override
    {
      static string_ref v("curl code domain");
      return v;
    }
    virtual bool _do_failure(const status_code<void> &code) const noexcept override { return CURLE_OK != static_cast<const curl_code &>(code).value(); }
    virtual bool _do_equivalent(const status_code<void> & /*unused*/, const status_code<void> & /*unused*/) const noexcept override { return false; }
    virtual generic_code _generic_code(const status_code<void> & /*unused*/) const noexcept override { return errc::unknown; }
    virtual _base::string_ref _do_message(const status_code<void> &code) const noexcept override
    {
      assert(code.domain() == *this);
      const value_type &v = static_cast<const curl_code &>(code).value();
      return _base::string_ref(curl_easy_strerror(v));
    }
    SYSTEM_ERROR2_NORETURN virtual void _do_throw_exception(const status_code<void> &code) const override
    {
      throw status_error<_curl_code_domain>(static_cast<const curl_code &>(code));
    }
  };
  constexpr _curl_code_domain curl_code_domain;
  inline constexpr const _curl_code_domain &_curl_code_domain::get() { return curl_code_domain; }

  class _curlm_code_domain final : public status_code_domain
  {
    using _base = status_code_domain;

  public:
    using value_type = CURLMcode;

    constexpr _curlm_code_domain()
        : _base("{301e6eff-30a7-84e3-4518-e52983cfe676}")
    {
    }
    static inline constexpr const _curlm_code_domain &get();

    virtual _base::string_ref name() const noexcept override
    {
      static string_ref v("curl code domain");
      return v;
    }
    virtual bool _do_failure(const status_code<void> &code) const noexcept override { return CURLM_OK != static_cast<const curlm_code &>(code).value(); }
    virtual bool _do_equivalent(const status_code<void> & /*unused*/, const status_code<void> & /*unused*/) const noexcept override { return false; }
    virtual generic_code _generic_code(const status_code<void> & /*unused*/) const noexcept override { return errc::unknown; }
    virtual _base::string_ref _do_message(const status_code<void> &code) const noexcept override
    {
      assert(code.domain() == *this);
      const value_type &v = static_cast<const curlm_code &>(code).value();
      return _base::string_ref(curl_multi_strerror(v));
    }
    SYSTEM_ERROR2_NORETURN virtual void _do_throw_exception(const status_code<void> &code) const override
    {
      throw status_error<_curlm_code_domain>(static_cast<const curlm_code &>(code));
    }
  };
  constexpr _curlm_code_domain curlm_code_domain;
  inline constexpr const _curlm_code_domain &_curlm_code_domain::get() { return curlm_code_domain; }

#define CURL_UNIQUE_NAME_GLUE2(x, y) x##y
#define CURL_UNIQUE_NAME_GLUE(x, y) CURL_UNIQUE_NAME_GLUE2(x, y)
#define CURL_UNIQUE_NAME CURL_UNIQUE_NAME_GLUE(_curl_unique_name_temporary, __COUNTER__)
#define CURL_CHECK2(unique, expr)                                                                                                                              \
  curl_code unique(expr);                                                                                                                                      \
  if(unique.failure())                                                                                                                                         \
  {                                                                                                                                                            \
    unique.throw_exception();                                                                                                                                  \
  }
#define CURL_CHECK(expr) CURL_CHECK2(CURL_UNIQUE_NAME, expr)
#define CURLM_CHECK2(unique, expr)                                                                                                                             \
  curlm_code unique(expr);                                                                                                                                     \
  if(unique.failure())                                                                                                                                         \
  {                                                                                                                                                            \
    unique.throw_exception();                                                                                                                                  \
  }
#define CURLM_CHECK(expr) CURLM_CHECK2(CURL_UNIQUE_NAME, expr)

  static struct curl_global_init_t
  {
    curl_global_init_t() { ::curl_global_init(CURL_GLOBAL_ALL); }
  } curl_global_init;

}  // namespace curl_status_codes