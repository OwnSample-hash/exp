option(MODULE_%s_STATIC "Build %s module static" ON)
option(MODULE_%s_DYNAMIC "Build %s module dynamic" OFF)

set(MOD_NAME %s)
set(MOD_SRC src/%s.cpp)

if(MODULE_%s_STATIC)
  add_library(${MOD_NAME}_static STATIC ${MOD_SRC})
  target_sources(${MOD_NAME}_static PUBLIC ${MOD_SRC})
  target_sources(${MOD_NAME}_static PUBLIC ${MOD_SRC})
  target_include_directories(${MOD_NAME}_static PUBLIC include ../../include/)
  target_link_libraries(${MOD_NAME}_static PRIVATE spdlog::spdlog)
  target_compile_definitions(${MOD_NAME}_static PRIVATE MODULE_%s_STATIC)
  target_compile_definitions(${MOD_NAME}_static PRIVATE MODULE_NAME="${MOD_NAME}")
endif()

if(MODULE_%s_DYNAMIC)
  add_library(${MOD_NAME}_dynamic SHARED ${MOD_SRC})
  target_sources(${MOD_NAME}_dynamic PUBLIC ${MOD_SRC})
  target_sources(${MOD_NAME}_dynamic PUBLIC ${MOD_SRC})
  target_include_directories(${MOD_NAME}_dynamic PUBLIC include ../../include/)
  target_link_libraries(${MOD_NAME}_dynamic PRIVATE spdlog::spdlog)
  target_compile_definitions(${MOD_NAME}_dynamic PRIVATE MODULE_%s_DYNAMIC)
  target_compile_definitions(${MOD_NAME}_dynamic PRIVATE MODULE_NAME="${MOD_NAME}")
endif()

enable_testing()
add_subdirectory(tests)
# Vim: set expandtab tabstop=2 shiftwidth=2:
