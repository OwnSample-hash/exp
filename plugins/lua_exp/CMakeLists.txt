if (LUA_EXP_ENABLE)
  set(MOD_NAME lua_exp)
  set(MOD_SRC 
    src/lua_exp.cpp
    src/loader.cpp
    src/tool.cpp
    src/lib.cpp
  )

  CPMAddPackage(
    NAME lua
    GIT_REPOSITORY https://github.com/lua/lua.git
    VERSION 5.5.1
    DOWNLOAD_ONLY YES
  )
  
  if (lua_ADDED)
    FILE(GLOB lua_sources ${lua_SOURCE_DIR}/*.c)
    list(REMOVE_ITEM lua_sources "${lua_SOURCE_DIR}/lua.c" "${lua_SOURCE_DIR}/luac.c"
     "${lua_SOURCE_DIR}/onelua.c")
    add_library(lua STATIC ${lua_sources})
    FILE(GLOB lua_headers ${lua_SOURCE_DIR}/*.h)

    target_include_directories(lua
      PUBLIC ${lua_headers}
    )

    target_compile_definitions(lua PRIVATE LUA_USE_LINUX)

    set_target_properties(lua PROPERTIES
      POSITION_INDEPENDENT_CODE ON
      C_STANDARD 99
      C_STANDARD_REQUIRED ON
      C_EXTENSIONS OFF
    )
  endif()

  add_library(${MOD_NAME} SHARED ${MOD_SRC})
  target_sources(${MOD_NAME} PUBLIC ${MOD_SRC})
  target_include_directories(${MOD_NAME} PUBLIC include ../../include/ ${lua_headers})
  target_link_libraries(${MOD_NAME} PRIVATE spdlog::spdlog lua nlohmann_json::nlohmann_json)
  target_compile_definitions(${MOD_NAME} PRIVATE MODULE_NAME="${MOD_NAME}" LUA_USE_LINUX LUA_USE_APICHECK)
  set_property(TARGET ${MOD_NAME} PROPERTY PREFIX "")
 
  add_pch(${MOD_NAME})

  add_custom_command(TARGET ${MOD_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE:${MOD_NAME}> ${CMAKE_CURRENT_BINARY_DIR}/../$<TARGET_FILE_NAME:${MOD_NAME}>
    COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_CURRENT_SOURCE_DIR}/lua ${CMAKE_BINARY_DIR}/${LUA_EXP_DIRECTORY}
    COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_CURRENT_SOURCE_DIR}/lua_lib ${CMAKE_BINARY_DIR}/${LUA_EXP_LIB_DIR}
  )
 
  if(ENABLE_TESTS)
   enable_testing()
   add_subdirectory(tests)
  endif()
endif()
# Vim: set expandtab tabstop=2 shiftwidth=2:
