option(MODULE_BASIC_TUI_STATIC "Build basic_tui module static" ON)
option(MODULE_BASIC_TUI_DYNAMIC "Build basic_tui module dynamic" OFF)

set(MOD_NAME basic_tui)
set(MOD_SRC src/basic_tui.cpp)

if(MODULE_BASIC_TUI_STATIC)
  add_library(${MOD_NAME}_static STATIC ${MOD_SRC})
  target_sources(${MOD_NAME}_static PUBLIC ${MOD_SRC})
  target_sources(${MOD_NAME}_static PUBLIC ${MOD_SRC})
  target_include_directories(${MOD_NAME}_static PUBLIC include ../../include/)
  target_link_libraries(${MOD_NAME}_static PRIVATE spdlog::spdlog)
  target_compile_definitions(${MOD_NAME}_static PRIVATE MODULE_BASIC_TUI_STATIC)
  target_compile_definitions(${MOD_NAME}_static PRIVATE MODULE_NAME="${MOD_NAME}")
endif()

if(MODULE_BASIC_TUI_DYNAMIC)
  add_library(${MOD_NAME}_dynamic SHARED ${MOD_SRC})
  target_sources(${MOD_NAME}_dynamic PUBLIC ${MOD_SRC})
  target_sources(${MOD_NAME}_dynamic PUBLIC ${MOD_SRC})
  target_include_directories(${MOD_NAME}_dynamic PUBLIC include ../../include/)
  target_link_libraries(${MOD_NAME}_dynamic PRIVATE spdlog::spdlog)
  target_compile_definitions(${MOD_NAME}_dynamic PRIVATE MODULE_BASIC_TUI_DYNAMIC)
  target_compile_definitions(${MOD_NAME}_dynamic PRIVATE MODULE_NAME="${MOD_NAME}")
endif()

enable_testing()
add_subdirectory(tests)
# Vim: set expandtab tabstop=2 shiftwidth=2:
