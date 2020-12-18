cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

project(libcurl-cxx-std-networking-integration VERSION 1.0 LANGUAGES C CXX)

set(BUILD_SHARED_LIBS Off)
add_subdirectory("curl" EXCLUDE_FROM_ALL)

add_executable(libcurl-cxx-std-networking-integration "libcurl-cxx-std-networking-integration.cpp")
target_compile_features(libcurl-cxx-std-networking-integration PRIVATE cxx_std_17)
target_include_directories(libcurl-cxx-std-networking-integration PRIVATE
  "asio/asio/include"
)
target_link_libraries(libcurl-cxx-std-networking-integration PRIVATE CURL::libcurl)
