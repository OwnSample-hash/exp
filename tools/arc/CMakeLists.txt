set(arc_SOURCES
  arc.cpp
)

add_library(arc SHARED
  ${arc_SOURCES}
)

if(MAKE_EXECUTABLE_ARC)
  if(LINK_STATIC_ARC)
    add_executable(arc_exec arc_exec.cpp ${arc_SOURCES})
  else()
    add_executable(arc_exec arc_exec.cpp)
    target_link_libraries(arc_exec PRIVATE arc)
  endif()
  set_target_properties(arc_exec PROPERTIES OUTPUT_NAME "arc")

  add_custom_command(TARGET arc_exec POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/tools/bin
  )

  add_custom_command(TARGET arc_exec POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE:arc_exec> ${CMAKE_BINARY_DIR}/tools/bin/$<TARGET_FILE_NAME:arc_exec>
  )
endif()

# Vim: set expandtab tabstop=2 shiftwidth=2:
