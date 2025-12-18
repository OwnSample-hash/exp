enable_testing()
add_executable(test_example test_example.cpp)
target_link_libraries(test_example PRIVATE Catch2::Catch2WithMain)
if(MODULE_EXAMPLE_STATIC)
  target_link_libraries(test_example PRIVATE example_static)
endif()


add_test(NAME example_module_tests COMMAND test_example)
# Vim: set expandtab tabstop=2 shiftwidth=2:
