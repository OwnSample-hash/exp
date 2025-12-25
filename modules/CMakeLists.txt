enable_testing()
file(GLOB MODULE_DIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} */)
foreach(MDIR ${MODULE_DIRS})
  set(FOUND_TEMPLATE 0)
  string(FIND ${MDIR} "template" FOUND_TEMPLATE)
  if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${MDIR} AND FOUND_TEMPLATE EQUAL -1)
    add_subdirectory(${MDIR})
  endif()
endforeach()

# Vim: set expandtab tabstop=2 shiftwidth=2:
