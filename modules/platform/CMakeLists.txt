option(MODULE_PLATFORM_STATIC "Build platform module static" ON)
option(MODULE_PLATFORM_DYNAMIC "Build platform module dynamic" OFF)


set(MOD_NAME platform)
set(MOD_SRC src/platform.cpp)


if(MODULE_PLATFORM_STATIC)
  add_library(${MOD_NAME}_static STATIC ${MOD_SRC})
  target_sources(${MOD_NAME}_static PUBLIC ${MOD_SRC})
endif()


if(MODULE_PLATFORM_DYNAMIC)
  add_library(${MOD_NAME}_dynamic SHARED ${MOD_SRC})
  target_sources(${MOD_NAME}_dynamic PUBLIC ${MOD_SRC})
endif()

enable_testing()
add_subdirectory(test)
# Vim: set expandtab tabstop=2 shiftwidth=2:
