if(PYTHON_EXP_ENABLE)
  set(MOD_NAME python_exp)
  set(MOD_SRC 
    src/python_exp.cpp
    src/tool.cpp
    src/pylib.cpp
    src/interactive.cpp
  )

  find_package(Python REQUIRED COMPONENTS Interpreter Development)

  execute_process(COMMAND uuidgen OUTPUT_VARIABLE interactive_UUID OUTPUT_STRIP_TRAILING_WHITESPACE)

  add_library(${MOD_NAME} SHARED ${MOD_SRC})
  target_sources(${MOD_NAME} PUBLIC ${MOD_SRC})
  target_sources(${MOD_NAME} PUBLIC ${MOD_SRC})
  target_include_directories(${MOD_NAME} PUBLIC include ../../include/)
  target_link_libraries(${MOD_NAME} PRIVATE spdlog::spdlog Python::Python)
  target_compile_definitions(${MOD_NAME} PRIVATE MODULE_NAME="${MOD_NAME}" INTERACTIVE_UUID="${interactive_UUID}")
  set_property(TARGET ${MOD_NAME} PROPERTY PREFIX "")

  add_custom_command(TARGET ${MOD_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE:${MOD_NAME}> ${CMAKE_CURRENT_BINARY_DIR}/../$<TARGET_FILE_NAME:${MOD_NAME}>
    COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_CURRENT_SOURCE_DIR}/python ${CMAKE_BINARY_DIR}/${PYTHON_EXP_DIRECTORY}
    COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_CURRENT_SOURCE_DIR}/python_lib ${CMAKE_BINARY_DIR}/${PYTHON_EXP_LIB_DIR}
  )

  if(ENABLE_TESTS)
   enable_testing()
   add_subdirectory(tests)
  endif()
endif()
# Vim: set expandtab tabstop=2 shiftwidth=2:
