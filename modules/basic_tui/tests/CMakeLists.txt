enable_testing()
add_executable(test_basic_tui test_basic_tui.cpp)
target_link_libraries(test_basic_tui PRIVATE Catch2::Catch2WithMain)

if(MODULE_BASIC_TUI_STATIC)
  target_link_libraries(test_basic_tui PRIVATE basic_tui_static)
  target_compile_definitions(test_basic_tui PRIVATE MODULE_BASIC_TUI_STATIC)
endif()

if (MODULE_BASIC_TUI_DYNAMIC)
  target_link_libraries(test_basic_tui PRIVATE basic_tui_dynamic)
  target_compile_definitions(test_basic_tui PRIVATE MODULE_BASIC_TUI_DYNAMIC)
endif()

add_test(NAME basic_tui_module_tests COMMAND test_basic_tui)
# Vim: set expandtab tabstop=2 shiftwidth=2:
