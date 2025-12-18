option(MODULE_EXAMPLE_STATIC "Build example module static" ON)
option(MODULE_EXAMPLE_DYNAMIC "Build example module dynamic" OFF)


set(MOD_NAME example)
set(MOD_SRC src/example.cpp)


if(MODULE_EXAMPLE_STATIC)
add_library(${MOD_NAME}_static STATIC ${MOD_SRC})
target_sources(${MOD_NAME}_static PUBLIC ${MOD_SRC})
target_link_libraries(${MOD_NAME}_static PRIVATE spdlog::spdlog)
endif()


if(MODULE_EXAMPLE_DYNAMIC)
add_library(${MOD_NAME}_dynamic SHARED ${MOD_SRC})
target_sources(${MOD_NAME}_dynamic PUBLIC ${MOD_SRC})
target_link_libraries(${MOD_NAME}_dynamic PRIVATE spdlog::spdlog)
endif()

enable_testing()
add_subdirectory(test)
# Vim: set expandtab tabstop=2 shiftwidth=2:
