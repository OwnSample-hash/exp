file(GLOB_RECURSE main_app_SOURCE src/*.cpp)

add_executable(main_app
  ${main_app_SOURCE}
)
target_link_libraries(main_app PRIVATE spdlog::spdlog)
target_include_directories(main_app PRIVATE ../include ./include)
set_target_properties(main_app PROPERTIES POSITION_INDEPENDENT_CODE ON)

set_property(TARGET main_app PROPERTY ENABLE_EXPORTS ON)

add_pch(main_app)

# Vim: set expandtab tabstop=2 shiftwidth=2:
