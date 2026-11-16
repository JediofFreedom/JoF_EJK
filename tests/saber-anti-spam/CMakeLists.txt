cmake_minimum_required(VERSION 3.16)
project(saber_anti_spam C)

get_filename_component(repo "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
configure_file("${repo}/codemp/versionfile/jofversion.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/jofversion.h" @ONLY)

# Compile the real saber toggle budget and the local view's weapon switch sounds. The end
# marker is searched for after the start, since it may also appear earlier in the file.
function(extract path first last)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${repo}/${path}")
  file(READ "${repo}/${path}" source)
  string(FIND "${source}" "${first}" start)
  if(start LESS 0)
    message(FATAL_ERROR "Cannot extract ${first}")
  endif()
  string(SUBSTRING "${source}" ${start} -1 rest)
  string(FIND "${rest}" "${last}" length)
  if(length LESS 0)
    message(FATAL_ERROR "Cannot find the end of ${first}")
  endif()
  string(SUBSTRING "${rest}" 0 ${length} part)
  file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/actual.h" "${part}\n")
endfunction()

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/actual.h" "")
extract(codemp/cgame/cg_event.c "#define SABER_TOGGLE_BURST" "// Model voice lines a player can fire off at will")
extract(codemp/cgame/cg_weapons.c "void CG_CheckPlayerG2Weapons(" "Ghoul2 Insert End")
# The extract ends inside the comment that follows the function.
file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/actual.h" "*/\n")

add_executable(saber_anti_spam check.c)
target_compile_definitions(saber_anti_spam PRIVATE _CGAME)
target_include_directories(saber_anti_spam PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}" "${repo}/codemp" "${repo}/shared")
enable_testing()
add_test(NAME saber_anti_spam COMMAND saber_anti_spam)
