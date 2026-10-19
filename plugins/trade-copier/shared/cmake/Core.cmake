include_guard(GLOBAL)

# Compile one platform-specific instance of the SDK-independent implementation.
# Distinct target prefixes allow both platforms to be built in one CMake tree;
# their configuration codecs and execution evidence remain platform-specific.
function(tradecopier_add_core)
  cmake_parse_arguments(TC "" "TARGET_PREFIX;PLATFORM;EXAMPLE_CONFIG" "" ${ARGN})
  if(TC_UNPARSED_ARGUMENTS OR NOT TC_TARGET_PREFIX OR NOT TC_PLATFORM)
    message(FATAL_ERROR "tradecopier_add_core requires TARGET_PREFIX and PLATFORM")
  endif()
  if(NOT TC_PLATFORM STREQUAL "mt5" AND NOT TC_PLATFORM STREQUAL "mt4")
    message(FATAL_ERROR "Unsupported trade-copier platform: ${TC_PLATFORM}")
  endif()

  set(shared_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
  add_library(${TC_TARGET_PREFIX}_core STATIC "${shared_root}/core/src/core.cpp")
  target_include_directories(${TC_TARGET_PREFIX}_core PUBLIC "${shared_root}/core/include")
  target_compile_definitions(${TC_TARGET_PREFIX}_core PUBLIC TRADECOPIER_PLATFORM_NAME="${TC_PLATFORM}")

  add_library(${TC_TARGET_PREFIX}_config STATIC
    "${shared_root}/core/src/config.cpp" "${shared_root}/core/src/symbols.cpp")
  target_link_libraries(${TC_TARGET_PREFIX}_config PUBLIC ${TC_TARGET_PREFIX}_core)

  find_package(Threads REQUIRED)
  add_library(${TC_TARGET_PREFIX}_execution STATIC
    "${shared_root}/core/src/executor.cpp"
    "${shared_root}/core/src/journal.cpp"
    "${shared_root}/core/src/management.cpp")
  target_link_libraries(${TC_TARGET_PREFIX}_execution PUBLIC ${TC_TARGET_PREFIX}_config Threads::Threads)

  foreach(component core config execution)
    target_compile_features(${TC_TARGET_PREFIX}_${component} PUBLIC cxx_std_17)
    set_target_properties(${TC_TARGET_PREFIX}_${component} PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC)
      target_compile_options(${TC_TARGET_PREFIX}_${component} PRIVATE /W4 /permissive- /utf-8)
    else()
      target_compile_options(${TC_TARGET_PREFIX}_${component} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
  endforeach()

  if(BUILD_TESTING)
    foreach(component core config)
      add_executable(${TC_TARGET_PREFIX}_${component}_tests "${shared_root}/tests/${component}_tests.cpp")
      target_link_libraries(${TC_TARGET_PREFIX}_${component}_tests PRIVATE ${TC_TARGET_PREFIX}_${component})
      if(MSVC)
        target_compile_options(${TC_TARGET_PREFIX}_${component}_tests PRIVATE /W4 /permissive- /utf-8)
      else()
        target_compile_options(${TC_TARGET_PREFIX}_${component}_tests PRIVATE -Wall -Wextra -Wpedantic)
      endif()
    endforeach()
    add_test(NAME ${TC_TARGET_PREFIX}_core_tests COMMAND ${TC_TARGET_PREFIX}_core_tests)
    if(TC_EXAMPLE_CONFIG)
      add_test(NAME ${TC_TARGET_PREFIX}_config_tests COMMAND ${TC_TARGET_PREFIX}_config_tests "${TC_EXAMPLE_CONFIG}")
    else()
      add_test(NAME ${TC_TARGET_PREFIX}_config_tests COMMAND ${TC_TARGET_PREFIX}_config_tests)
    endif()
    add_executable(${TC_TARGET_PREFIX}_unsubmitted_refusal_tests "${shared_root}/tests/unsubmitted_refusal_tests.cpp")
    target_link_libraries(${TC_TARGET_PREFIX}_unsubmitted_refusal_tests PRIVATE ${TC_TARGET_PREFIX}_execution)
    add_test(NAME ${TC_TARGET_PREFIX}_unsubmitted_refusal_tests COMMAND ${TC_TARGET_PREFIX}_unsubmitted_refusal_tests)
  endif()
endfunction()
