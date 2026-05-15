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

if(CMAKE_BUILD_TYPE)
  add_compile_options(-ggdb)
  add_link_options(-ggdb)
endif()

if(ENABLE_SANITIZERS)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# Global modules
add_subdirectory(plugins)
add_subdirectory(app)
if (ENABLE_TESTS)
  CPMAddPackage("gh:catchorg/Catch2@3.11.0")
  enable_testing()
endif()
# Vim: set expandtab tabstop=2 shiftwidth=2:
