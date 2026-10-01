find_program(YARN_EXECUTABLE yarn REQUIRED)

set(SVELTEKIT_BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build")
set(SVELTEKIT_ARCHIVE "${CMAKE_CURRENT_BINARY_DIR}/webui.arc")
set(SVELTEKIT_ARCHIVE "${CMAKE_CURRENT_BINARY_DIR}/webui.arc" PARENT_SCOPE)
file(GLOB_RECURSE SVELTEKIT_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*"
)

add_custom_command(
  OUTPUT ${SVELTEKIT_ARCHIVE}
  COMMAND ${YARN_EXECUTABLE} install > /dev/null 2>&1
  COMMAND ${YARN_EXECUTABLE} build   > /dev/null 2>&1
  COMMAND $<TARGET_FILE:arc_exec> c "${SVELTEKIT_ARCHIVE}" "${SVELTEKIT_BUILD_DIR}" > /dev/null 2>&1
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS ${SVELTEKIT_SOURCES}
  COMMENT "Building SvelteKit project with yarn and archiving output"
  VERBATIM
)

add_custom_target(sveltekit_build DEPENDS ${SVELTEKIT_ARCHIVE})
add_dependencies(sveltekit_build arc_exec)
add_dependencies(${MOD_NAME} sveltekit_build)
# Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
