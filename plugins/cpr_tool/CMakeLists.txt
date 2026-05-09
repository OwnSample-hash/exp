if (ENABLE_CPR_TOOL)
  set(MOD_NAME cpr_tool)
  set(MOD_SRC 
    src/cpr_tool.cpp
    src/tool.cpp
  )

  CPMAddPackage("gh:libcpr/cpr#1.14.2")

  add_library(${MOD_NAME} SHARED ${MOD_SRC})
  target_sources(${MOD_NAME} PUBLIC ${MOD_SRC})
  target_sources(${MOD_NAME} PUBLIC ${MOD_SRC})
  target_include_directories(${MOD_NAME} PUBLIC include ../../include/)
  target_link_libraries(${MOD_NAME} PRIVATE spdlog::spdlog cpr::cpr nlohmann_json::nlohmann_json)
  target_compile_definitions(${MOD_NAME} PRIVATE MODULE_NAME="${MOD_NAME}")
  set_property(TARGET ${MOD_NAME} PROPERTY PREFIX "")
 
  add_custom_command(TARGET ${MOD_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE:${MOD_NAME}> ${CMAKE_CURRENT_BINARY_DIR}/../$<TARGET_FILE_NAME:${MOD_NAME}>
  )
 
  if(ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
  endif()
endif()
# Vim: set expandtab tabstop=2 shiftwidth=2:
