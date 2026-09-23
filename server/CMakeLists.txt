if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "TradeCopierSdkProbe requires Windows and the MSVC toolchain.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "TradeCopierSdkProbe requires an x64 build (use -A x64).")
endif()
if(CMAKE_GENERATOR_PLATFORM AND NOT CMAKE_GENERATOR_PLATFORM STREQUAL "x64")
  message(FATAL_ERROR "TradeCopierSdkProbe currently supports only the x64 generator platform.")
endif()
if(NOT MT5_SERVER_SDK_DIR)
  message(FATAL_ERROR "Set MT5_SERVER_SDK_DIR to the SDK directory containing Include/MT5APIServer.h.")
endif()

set(_probe_sdk_header "${MT5_SERVER_SDK_DIR}/Include/MT5APIServer.h")
if(NOT EXISTS "${_probe_sdk_header}")
  message(FATAL_ERROR "MT5 Server API header not found: ${_probe_sdk_header}")
endif()
file(STRINGS "${_probe_sdk_header}" _probe_sdk_version
     REGEX "^#define[ \t]+MTServerAPIVersion[ \t]+[0-9]+")
if(NOT _probe_sdk_version MATCHES "MTServerAPIVersion[ \t]+6182([ \t]|$)")
  message(FATAL_ERROR "This reviewed probe targets the supplied Server API 6182 SDK. Do not edit the SDK version to bypass this check.")
endif()

if(TRADECOPIER_BUILD_SDK_PROBE)
add_library(TradeCopierSdkProbe SHARED probe.cpp)
target_include_directories(TradeCopierSdkProbe SYSTEM PRIVATE "${MT5_SERVER_SDK_DIR}/Include")
target_compile_features(TradeCopierSdkProbe PRIVATE cxx_std_17)
target_compile_definitions(TradeCopierSdkProbe PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
  _WIN32_WINNT=0x0601 WINVER=0x0601)
# /EHa makes catch (...) boundaries cover MSVC asynchronous exceptions as well.
# It does not make invalid memory accesses or a corrupt host recoverable.
target_compile_options(TradeCopierSdkProbe PRIVATE /W4 /permissive- /utf-8 /EHa)
set_target_properties(TradeCopierSdkProbe PROPERTIES
  CXX_EXTENSIONS OFF
  PREFIX ""
  OUTPUT_NAME "TradeCopierSdkProbe"
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# This target deliberately does not link the copier business core and contains
# no order submission, account mutation, equity query or network listener.
endif()

if(TRADECOPIER_BUILD_EXECUTOR)
  add_library(TradeCopierExecutor SHARED executor.cpp mt5_port.cpp)
  target_link_libraries(TradeCopierExecutor PRIVATE tradecopier_management_pipe bcrypt)
  target_include_directories(TradeCopierExecutor SYSTEM PRIVATE "${MT5_SERVER_SDK_DIR}/Include")
  target_compile_features(TradeCopierExecutor PRIVATE cxx_std_17)
  target_compile_definitions(TradeCopierExecutor PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _WIN32_WINNT=0x0601 WINVER=0x0601)
  target_compile_options(TradeCopierExecutor PRIVATE /W4 /permissive- /utf-8 /EHa)
  set_target_properties(TradeCopierExecutor PROPERTIES
    CXX_EXTENSIONS OFF PREFIX "" OUTPUT_NAME "TradeCopierExecutor"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
