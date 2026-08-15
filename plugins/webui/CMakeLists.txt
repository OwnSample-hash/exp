if(ENABLE_WEBUI)
  set(MOD_NAME webui)
  set(MOD_SRC 
    src/webui.cpp
    src/tool.cpp
    src/ws_loop.cpp
    src/convert.cpp
    src/b64.cpp
  )

  CPMAddPackage("gh:yhirose/cpp-httplib@0.46.1")

  find_package(OpenSSL COMPONENTS SSL)

  add_library(${MOD_NAME} SHARED ${MOD_SRC})
  target_sources(${MOD_NAME} PUBLIC ${MOD_SRC})
  target_compile_options(${MOD_NAME} PRIVATE -Wno-c23-extensions)
  target_include_directories(${MOD_NAME} PUBLIC include ../../include/)
  target_link_libraries(${MOD_NAME} PRIVATE spdlog::spdlog httplib OpenSSL::SSL nlohmann_json::nlohmann_json arc::arc)
  target_compile_definitions(${MOD_NAME} PRIVATE MODULE_NAME="${MOD_NAME}" CPPHTTPLIB_OPENSSL_SUPPORT)
  set_property(TARGET ${MOD_NAME} PROPERTY PREFIX "")

  add_pch(${MOD_NAME})

  add_custom_command(TARGET ${MOD_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE:${MOD_NAME}> ${CMAKE_CURRENT_BINARY_DIR}/../$<TARGET_FILE_NAME:${MOD_NAME}>
  )

  add_subdirectory(web)

  target_compile_definitions(${MOD_NAME} PRIVATE "ARCHIVE=\"${SVELTEKIT_ARCHIVE}\"")

  if(ENABLE_TESTS)
   enable_testing()
   add_subdirectory(tests)
  endif()
endif()
# Vim: set expandtab tabstop=2 shiftwidth=2:
