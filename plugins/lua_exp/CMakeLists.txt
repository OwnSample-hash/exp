if (ENABLE_LUA_EXP)
  message(STATUS "Building Lua Expression Module")
  set(MOD_NAME lua_exp)
  set(MOD_SRC 
    src/lua_exp.cpp
    src/loader.cpp
  )

  CPMAddPackage(
    NAME lua
    GIT_REPOSITORY https://github.com/lua/lua.git
    VERSION 5.5.0
    DOWNLOAD_ONLY YES
  )
  
  if (lua_ADDED)
    # lua has no CMake support, so we create our own target
  
    FILE(GLOB lua_sources ${lua_SOURCE_DIR}/*.c)
    list(REMOVE_ITEM lua_sources "${lua_SOURCE_DIR}/lua.c" "${lua_SOURCE_DIR}/luac.c")
    add_library(lua STATIC ${lua_sources})
  
    target_include_directories(lua
      PUBLIC
        $<BUILD_INTERFACE:${lua_SOURCE_DIR}>
    )
  endif()
 
  add_library(${MOD_NAME} SHARED ${MOD_SRC})
  target_sources(${MOD_NAME} PUBLIC ${MOD_SRC})
  target_sources(${MOD_NAME} PUBLIC ${MOD_SRC})
  target_include_directories(${MOD_NAME} PUBLIC include ../../include/ ${lua_SOURCE_DIR})
  target_link_libraries(${MOD_NAME} PRIVATE spdlog::spdlog lua)
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
