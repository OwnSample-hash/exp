if(ENCODER_ENABLE)
  set(MOD_NAME encoder)
  set(MOD_SRC
    src/encoder.cpp
    src/encoder_tool.cpp
    src/encoder_def.cpp
    src/pipeline.cpp
    src/codec.cpp
    src/process.cpp)

  add_library(${MOD_NAME} SHARED ${MOD_SRC})
  target_include_directories(${MOD_NAME} PUBLIC include ../../include/)
  target_link_libraries(${MOD_NAME} PRIVATE spdlog::spdlog yaml-cpp::yaml-cpp)
  target_compile_definitions(${MOD_NAME} PRIVATE MODULE_NAME="${MOD_NAME}")
  set_property(TARGET ${MOD_NAME} PROPERTY PREFIX "")

  add_custom_command(TARGET ${MOD_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE:${MOD_NAME}> ${CMAKE_CURRENT_BINARY_DIR}/../$<TARGET_FILE_NAME:${MOD_NAME}>
    COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_CURRENT_SOURCE_DIR}/encoders ${CMAKE_BINARY_DIR}/encoders
  )

  if(ENABLE_TESTS)
   enable_testing()
   add_subdirectory(tests)
  endif()
endif()
# Vim: set expandtab tabstop=2 shiftwidth=2:
