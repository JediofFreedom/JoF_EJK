# Upstream deliberately supports ExternalProject, not add_subdirectory.
# Keep its targets/configuration isolated while honoring toolchain flags.
include(ExternalProject)
get_filename_component(turbo_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(turbo_binary "${CMAKE_CURRENT_BINARY_DIR}/jpeg-turbo")
file(MAKE_DIRECTORY "${turbo_binary}")
set(turbo_args
  "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
  "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
  "-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON"
  "-DENABLE_SHARED:BOOL=OFF" "-DENABLE_STATIC:BOOL=ON"
  "-DWITH_TURBOJPEG:BOOL=OFF" "-DWITH_TOOLS:BOOL=OFF" "-DWITH_TESTS:BOOL=OFF")
if(MSVC_RUNTIME STREQUAL "static")
  list(APPEND turbo_args "-DWITH_CRT_DLL:BOOL=OFF")
else()
  list(APPEND turbo_args "-DWITH_CRT_DLL:BOOL=ON")
endif()
foreach(setting CMAKE_TOOLCHAIN_FILE CMAKE_OSX_ARCHITECTURES CMAKE_OSX_SYSROOT
    CMAKE_OSX_DEPLOYMENT_TARGET CMAKE_ASM_NASM_COMPILER CMAKE_MSVC_RUNTIME_LIBRARY
    CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
    CMAKE_C_FLAGS_RELWITHDEBINFO CMAKE_C_FLAGS_MINSIZEREL)
  if(DEFINED ${setting} AND NOT "${${setting}}" STREQUAL "")
    string(REPLACE ";" "|" turbo_value "${${setting}}")
    list(APPEND turbo_args "-D${setting}:STRING=${turbo_value}")
  endif()
endforeach()
if(MSVC)
  set(turbo_archive "${turbo_binary}/jpeg-static.lib")
else()
  set(turbo_archive "${turbo_binary}/libjpeg.a")
endif()
get_property(turbo_multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(turbo_multi)
  get_filename_component(turbo_name "${turbo_archive}" NAME)
  set(turbo_archive "${turbo_binary}/$<CONFIG>/${turbo_name}")
endif()
ExternalProject_Add(ejk_jpeg_turbo_build
  SOURCE_DIR "${turbo_root}/lib/jpeg-turbo"
  BINARY_DIR "${turbo_binary}"
  LIST_SEPARATOR "|"
  CMAKE_ARGS ${turbo_args}
  BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --config $<CONFIG> --target jpeg-static
  BUILD_BYPRODUCTS "${turbo_archive}"
  INSTALL_COMMAND ""
  UPDATE_COMMAND "")
add_library(ejk_jpeg_turbo STATIC IMPORTED GLOBAL)
if(turbo_multi)
  foreach(config IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${config}" upper)
    set_property(TARGET ejk_jpeg_turbo APPEND PROPERTY IMPORTED_CONFIGURATIONS "${config}")
    set_property(TARGET ejk_jpeg_turbo PROPERTY IMPORTED_LOCATION_${upper}
      "${turbo_binary}/${config}/${turbo_name}")
  endforeach()
else()
  set_property(TARGET ejk_jpeg_turbo PROPERTY IMPORTED_LOCATION "${turbo_archive}")
endif()
add_dependencies(ejk_jpeg_turbo ejk_jpeg_turbo_build)
set(JPEG_LIBRARIES ejk_jpeg_turbo)
set(JPEG_INCLUDE_DIR "${turbo_root}/lib/jpeg-turbo/src" "${turbo_binary}")
