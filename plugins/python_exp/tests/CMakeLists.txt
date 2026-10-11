enable_testing()

file(GLOB_RECURSE src_files ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
foreach(src_file ${src_files})
  get_filename_component(MOD_NAME ${src_file} NAME_WE)
  message(STATUS "Adding test: ${MOD_NAME}")
  add_executable(${MOD_NAME} ${src_file})
  target_link_libraries(${MOD_NAME} PRIVATE Catch2::Catch2WithMain)
  target_include_directories(${MOD_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/../include)
  add_test(NAME ${MOD_NAME}_test COMMAND ${MOD_NAME})
endforeach()

unset(src_files)

# Vim: set expandtab tabstop=2 shiftwidth=2:
