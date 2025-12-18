enable_testing()
add_executable(test_platform test_platform.cpp)
target_link_libraries(test_platform PRIVATE Catch2::Catch2WithMain)
if(MODULE_PLATFORM_STATIC)
  target_link_libraries(test_platform PRIVATE platform_static)
endif()


add_test(NAME platform_module_tests COMMAND test_platform)
# Vim: set expandtab tabstop=2 shiftwidth=2:
