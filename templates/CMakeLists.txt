set(MOD_NAME %s)
set(MOD_SRC src/%s.cpp)

add_library(${MOD_NAME} SHARED ${MOD_SRC})
target_sources(${MOD_NAME} PUBLIC ${MOD_SRC})
target_sources(${MOD_NAME} PUBLIC ${MOD_SRC})
target_include_directories(${MOD_NAME} PUBLIC include ../../include/)
target_link_libraries(${MOD_NAME} PRIVATE spdlog::spdlog)
target_compile_definitions(${MOD_NAME} PRIVATE MODULE_NAME="${MOD_NAME}")
set_property(TARGET ${MOD_NAME} PROPERTY PREFIX "")

enable_testing()
add_subdirectory(tests)
# Vim: set expandtab tabstop=2 shiftwidth=2:
