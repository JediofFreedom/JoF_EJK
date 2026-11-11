cmake_minimum_required(VERSION 3.16)
project(build_config_regressions NONE)
enable_testing()
add_test(NAME msvc_flags_reconfigure
  COMMAND "${CMAKE_COMMAND}" -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/../..
    -DTEST_BINARY=${CMAKE_CURRENT_BINARY_DIR}/reconfigure -P "${CMAKE_CURRENT_SOURCE_DIR}/check.cmake")
