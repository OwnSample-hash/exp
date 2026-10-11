enable_testing()

file(GLOB_RECURSE src_files src/*.cpp)
foreach(src_file src_files) 
    get_filename_component(MOD_NAME ${{src_file}} NAME_WE)
    add_executable(${{MOD_NAME}} ${{src_file}})
    target_link_libraries(${{MOD_NAME}} PRIVATE Catch2::Catch2WithMain)
    add_test(NAME ${{MOD_NAME}}_test COMMAND ${{MOD_NAME}})
endforeach()

# Vim: set expandtab tabstop=2 shiftwidth=2:
