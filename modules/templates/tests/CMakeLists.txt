enable_testing()
add_executable(test_%s test_%s.cpp)
target_link_libraries(test_%s PRIVATE Catch2::Catch2WithMain)

if(MODULE_%s_STATIC)
  target_link_libraries(test_%s PRIVATE %s_static)
  target_compile_definitions(test_%s PRIVATE MODULE_%s_STATIC)
endif()

if (MODULE_%s_DYNAMIC)
  target_link_libraries(test_%s PRIVATE %s_dynamic)
  target_compile_definitions(test_%s PRIVATE MODULE_%s_DYNAMIC)
endif()

add_test(NAME %s_module_tests COMMAND test_%s)
# Vim: set expandtab tabstop=2 shiftwidth=2:
