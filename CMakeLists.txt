cmake_minimum_required(VERSION 4.3)

include(cmake/Options.cmake OPTIONAL RESULT_VARIABLE OPTIONS_INCLUDED)

if(NOT OPTIONS_INCLUDED)
  message(FATAL_ERROR "Failed to include Options.cmake")
endif()

option(ENABLE_TESTS "Enable tests" ON)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(exp LANGUAGES C CXX VERSION 0.0.1 SPDX_LICENSE "GPL-3.0-or-later")

include(cmake/CPM.cmake)
CPMUsePackageLock("cpm-package-lock.cmake")


set(SPDLOG_BUILD_SHARED ON)
set(YAML_CPP_BUILD_TESTS OFF)
set(YAML_CPP_BUILD_CONTRIB OFF)
set(YAML_CPP_BUILD_TOOLS OFF)

CPMAddPackage("gh:gabime/spdlog@1.17.0")
CPMAddPackage("gh:nlohmann/json@3.12.0")
CPMAddPackage("gh:jbeder/yaml-cpp#yaml-cpp-0.9.0")

add_compile_definitions(
  ARGS_NOEXCEPT
  CPPHTTPLIB_SSL_ENABLED
)

if (ENABLE_PCH)
  message(STATUS "Precompiled headers enabled")
  if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(PCH_SUFFIX ".pch")
  elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(PCH_SUFFIX ".gch")
  else()
    message(FATAL_ERROR "Unsupported compiler for precompiled headers")
  endif()
  make_directory(${CMAKE_BINARY_DIR}/pch)
  set(PCH_FLAGS -std=gnu++26)

  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    list(APPEND PCH_FLAGS -ggdb)
  endif()

  if(ENABLE_SANITIZERS)
    list(APPEND PCH_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
  endif()
  add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/pch${PCH_SUFFIX}
    COMMAND ${CMAKE_CXX_COMPILER} ${PCH_FLAGS} -fPIC -o ${CMAKE_BINARY_DIR}/pch/pch.PIC${PCH_SUFFIX} ${CMAKE_SOURCE_DIR}/pch/pch.hpp
    DEPENDS ${CMAKE_SOURCE_DIR}/pch/pch.hpp
    COMMENT "Generating precompiled header (-fPIC)"
  )
  add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/pch${PCH_SUFFIX}
    COMMAND ${CMAKE_CXX_COMPILER} ${PCH_FLAGS} -fPIE -o ${CMAKE_BINARY_DIR}/pch/pch.PIE${PCH_SUFFIX} ${CMAKE_SOURCE_DIR}/pch/pch.hpp
    DEPENDS ${CMAKE_SOURCE_DIR}/pch/pch.hpp
    COMMENT "Generating precompiled header (-fPIE)"
  )
  add_custom_target(pch ALL DEPENDS
    ${CMAKE_BINARY_DIR}/pch/pch.PIC${PCH_SUFFIX}
    ${CMAKE_BINARY_DIR}/pch/pch.PIE${PCH_SUFFIX}
  )
endif()

function(is_executable_target out target)
  get_target_property(_type ${target} TYPE)
  if(_type STREQUAL "EXECUTABLE")
    set(${out} TRUE PARENT_SCOPE)
  else()
    set(${out} FALSE PARENT_SCOPE)
  endif()
endfunction()

function(add_pch target)
  if (NOT ENABLE_PCH)
    return()
  endif()
  message(STATUS "Adding precompiled header to target ${target}")
  is_executable_target(${target} is_executable)
  if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    if(is_executable)
      target_compile_options(${target} BEFORE PRIVATE -include-pch ${CMAKE_BINARY_DIR}/pch/pch.PIE${PCH_SUFFIX})
    else()
      target_compile_options(${target} BEFORE PRIVATE -include-pch ${CMAKE_BINARY_DIR}/pch/pch.PIC${PCH_SUFFIX})
    endif()
  elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if (is_executable)
      target_include_directories(${target} BEFORE PRIVATE ${CMAKE_SOURCE_DIR}/pch)
      target_include_directories(${target} BEFORE PRIVATE ${CMAKE_BINARY_DIR}/pch)
      target_compile_options(${target} BEFORE PRIVATE -include pch.PIE.hpp)
    else()
      target_include_directories(${target} BEFORE PRIVATE ${CMAKE_SOURCE_DIR}/pch)
      target_include_directories(${target} BEFORE PRIVATE ${CMAKE_BINARY_DIR}/pch)
      target_compile_options(${target} BEFORE PRIVATE -include pch.PIC.hpp)
    endif()
  else()
    message(FATAL_ERROR "Unsupported compiler for precompiled headers")
  endif()
  # Add dependency to ensure the target is built after the precompiled header
  add_dependencies(${target} pch)
endfunction()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-ggdb)
  add_link_options(-ggdb)
endif()

if(ENABLE_SANITIZERS)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# Global modules
add_subdirectory(app)
message(STATUS "Adding global modules")
add_subdirectory(plugins)
if (ENABLE_TESTS)
  CPMAddPackage("gh:catchorg/Catch2@3.11.0")
  enable_testing()
endif()
# Vim: set expandtab tabstop=2 shiftwidth=2:
