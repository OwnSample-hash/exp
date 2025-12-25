add_executable(main_app src/main.cpp)
target_link_libraries(main_app PRIVATE spdlog::spdlog)
target_include_directories(main_app PRIVATE ../include)

target_sources(main_app PUBLIC)

if(MODULE_TEST_STATIC)
  target_link_libraries(main_app PRIVATE test_static)
endif()
if(MODULE_PLATFORM_STATIC)
  target_link_libraries(main_app PRIVATE platform_static)
endif()
# Vim: set expandtab tabstop=2 shiftwidth=2:
