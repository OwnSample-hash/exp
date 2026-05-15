enable_testing()
set(MOD_NAME test_bini)
set(MOD_SRC src/bini.cpp)
add_executable(${MOD_NAME} ${MOD_SRC})
target_link_libraries(${MOD_NAME} PRIVATE Catch2::Catch2WithMain)

if(MODULE_BINI_STATIC)
  target_link_libraries(${MOD_NAME} PRIVATE ${MOD_NAME}_static)
  target_compile_definitions(${MOD_NAME} PRIVATE MODULE_BINI_STATIC)
endif()

if (MODULE_BINI_DYNAMIC)
  target_link_libraries(${MOD_NAME} PRIVATE ${MOD_NAME}_dynamic)
  target_compile_definitions(${MOD_NAME} PRIVATE MODULE_BINI_DYNAMIC)
endif()

add_test(NAME ${MOD_NAME}_module_tests COMMAND ${MOD_NAME})
# Vim: set expandtab tabstop=2 shiftwidth=2:
