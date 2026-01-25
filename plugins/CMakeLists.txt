enable_testing()
file(GLOB MODULE_DIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} */)
foreach(MDIR ${MODULE_DIRS})
  if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${MDIR})
    add_subdirectory(${MDIR})
  endif()
endforeach()

# Vim: set expandtab tabstop=2 shiftwidth=2:
