

if(${ARC_FUSE_ENABLE})
  set(arc_SOURCES
    src/arc.cpp
    src/arc_fuse.cpp
    src/arc_mount_fuse.cpp
  )
  add_library(arc_shared SHARED ${arc_SOURCES})
  add_library(arc_static STATIC ${arc_SOURCES})
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(FUSE REQUIRED IMPORTED_TARGET fuse3)
  target_link_libraries(arc_shared PUBLIC PkgConfig::FUSE)
  target_link_libraries(arc_static PUBLIC PkgConfig::FUSE)
  set(FUSE_USE_VERSION 31)
  target_compile_definitions(arc_shared PUBLIC FUSE_USE_VERSION=${FUSE_USE_VERSION})
  target_compile_definitions(arc_static PUBLIC FUSE_USE_VERSION=${FUSE_USE_VERSION})
else()
  set(arc_SOURCES
    src/arc.cpp
    src/arc_fuse.cpp
    src/arc_mount_extract.cpp
  )
  add_library(arc_shared SHARED ${arc_SOURCES})
  add_library(arc_static STATIC ${arc_SOURCES})
endif()

target_include_directories(arc_shared PUBLIC 
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include/>
)
target_include_directories(arc_static PUBLIC 
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include/>
)

set_target_properties(arc_shared PROPERTIES OUTPUT_NAME "arc" POSITION_INDEPENDENT_CODE ON)
set_target_properties(arc_static PROPERTIES OUTPUT_NAME "arc" POSITION_INDEPENDENT_CODE ON)

install(TARGETS arc_shared arc_static
  EXPORT arcTargets
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
)
install(DIRECTORY include/ DESTINATION include)
install(EXPORT arcTargets
  FILE arcTargets.cmake
  NAMESPACE arc::
  DESTINATION lib/cmake/arc
)

add_library(arc::shared ALIAS arc_shared)
add_library(arc::static ALIAS arc_static)
add_library(arc::arc    ALIAS arc_static)

if(ARC_MAKE_EXECUTABLE)
  if(ARC_LINK_STATIC)
    add_executable(arc_exec src/arc_exec.cpp ${arc_SOURCES})
    target_link_libraries(arc_exec PRIVATE arc_static)
  else()
    add_executable(arc_exec src/arc_exec.cpp)
    target_link_libraries(arc_exec PRIVATE arc)
  endif()


  target_include_directories(arc_exec PUBLIC include/ ../../include/)
  set_target_properties(arc_exec PROPERTIES OUTPUT_NAME "arc")

  add_custom_command(TARGET arc_exec POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/tools/bin
  )

  add_custom_command(TARGET arc_exec POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE:arc_exec> ${CMAKE_BINARY_DIR}/tools/bin/$<TARGET_FILE_NAME:arc_exec>
  )
endif()

if (ENABLE_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# Vim: set expandtab tabstop=2 shiftwidth=2:
