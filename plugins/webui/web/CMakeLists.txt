find_program(YARN_EXECUTABLE yarn REQUIRED)

function(validate_arc_executable validator_result_var item)
  message(STATUS "Validating arc executable: ${item}")
  execute_process(
    COMMAND ${item} v
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error_output
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
  )
  if (NOT result EQUAL 0)
    set(${validator_result_var} FAlSE PARENT_SCOPE)
  endif()
endfunction()

message(STATUS "Searching for arc executable in ${CMAKE_BINARY_DIR}/tools/bin")
find_program(ARC_EXECUTABLE arc
  PATHS ${CMAKE_BINARY_DIR}/tools/bin
  VALIDATOR validate_arc_executable
  REQUIRED
)

set(SVELTEKIT_BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build")
set(SVELTEKIT_ARCHIVE "${CMAKE_CURRENT_BINARY_DIR}/webui.arc")

add_custom_target(sveltekit_build
  COMMAND ${YARN_EXECUTABLE} install
  COMMAND ${YARN_EXECUTABLE} build
  COMMAND ${ARC_EXECUTABLE} c "${SVELTEKIT_ARCHIVE}" "${SVELTEKIT_BUILD_DIR}"
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Building SvelteKit project with yarn and archiving output"
  BYPRODUCTS ${SVELTEKIT_ARCHIVE}
  VERBATIM
)

add_dependencies(sveltekit_build arc_exec)
add_dependencies(${MOD_NAME} sveltekit_build)
# Vim: set expandtab tabstop=2 shiftwidth=2:
