cmake_minimum_required(VERSION 4.3)

option(ENABLE_TESTS "Enable tests" ON)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

project(exp LANGUAGES C CXX VERSION 0.0.1 SPDX_LICENSE "GPL-3.0-or-later")

include(cmake/CPM.cmake)
CPMUsePackageLock("cpm-package-lock.cmake")
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SPDLOG_BUILD_SHARED ON)

CPMAddPackage("gh:gabime/spdlog@1.17.0")

add_compile_options(-ggdb)

# Global modules
add_subdirectory(plugins)
add_subdirectory(app)
if (ENABLE_TESTS)
  CPMAddPackage("gh:catchorg/Catch2@3.11.0")
  enable_testing()
endif()
# Vim: set expandtab tabstop=2 shiftwidth=2:
