cmake_minimum_required(VERSION 3.26)

option(ENABLE_TESTS "Enable tests" ON)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

project(exp LANGUAGES C CXX)

include(cmake/CPM.cmake)
CPMUsePackageLock("cpm-package-lock.cmake")
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SPDLOG_BUILD_SHARED ON)

CPMAddPackage("gh:gabime/spdlog@1.16.0")
CPMAddPackage("gh:p-ranav/argparse@3.2")

add_compile_options(-ggdb)

# Global modules
add_subdirectory(plugins)
add_subdirectory(app)
if (ENABLE_TESTS)
  CPMAddPackage("gh:catchorg/Catch2@3.11.0")
  enable_testing()
endif()
# Vim: set expandtab tabstop=2 shiftwidth=2:
