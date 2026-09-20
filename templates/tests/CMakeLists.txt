enable_testing()
set(MOD_NAME test_{CONFIG_NEW_MODULE_NAME_UP})
set(MOD_SRC src/test_{CONFIG_NEW_MODULE_NAME}.cpp)
add_executable(${{MOD_NAME}} ${{MOD_SRC}})
target_link_libraries(${{MOD_NAME}} PRIVATE Catch2::Catch2WithMain)

if(MODULE_%s_STATIC)
  target_link_libraries(${{MOD_NAME}} PRIVATE ${{MOD_NAME}}_static)
  target_compile_definitions(${{MOD_NAME}} PRIVATE MODULE_%s_STATIC)
endif()

if (MODULE_%s_DYNAMIC)
  target_link_libraries(${{MOD_NAME}} PRIVATE ${{MOD_NAME}}_dynamic)
  target_compile_definitions(${{MOD_NAME}} PRIVATE MODULE_%s_DYNAMIC)
endif()

add_test(NAME ${{MOD_NAME}}_module_tests COMMAND ${{MOD_NAME}})
# Vim: set expandtab tabstop=2 shiftwidth=2:
