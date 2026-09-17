if(BINI_ENABLE)
  set(MOD_NAME bini)
  set(MOD_SRC 
    src/bini.cpp
    src/summary.cpp
  )

  find_package(LLVM REQUIRED CONFIG COMPONENTS Object Support BinaryFormat)

  add_library(${MOD_NAME} SHARED ${MOD_SRC})
  target_sources(${MOD_NAME} PUBLIC ${MOD_SRC})
  target_sources(${MOD_NAME} PUBLIC ${MOD_SRC})
  target_include_directories(${MOD_NAME} PUBLIC include ../../include/)
  target_link_libraries(${MOD_NAME} PRIVATE spdlog::spdlog 
    LLVM
  )
  target_compile_definitions(${MOD_NAME} PRIVATE MODULE_NAME="${MOD_NAME}")
  set_property(TARGET ${MOD_NAME} PROPERTY PREFIX "")

  add_pch(${MOD_NAME})

  add_custom_command(TARGET ${MOD_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE:${MOD_NAME}> ${CMAKE_CURRENT_BINARY_DIR}/../$<TARGET_FILE_NAME:${MOD_NAME}>
  )

  if(ENABLE_TESTS)
   enable_testing()
   add_subdirectory(tests)
  endif()
endif()
# Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
