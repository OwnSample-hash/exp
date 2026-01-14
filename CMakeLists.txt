cmake_minimum_required(VERSION 3.26)
project(exp LANGUAGES CXX)

include(cmake/CPM.cmake)
CPMUsePackageLock("cpm-package-lock.cmake")
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

CPMAddPackage("gh:gabime/spdlog@1.16.0")
CPMAddPackage("gh:catchorg/Catch2@3.11.0")

# Global modules
add_subdirectory(modules)
add_subdirectory(app)
enable_testing()
# Vim: set expandtab tabstop=2 shiftwidth=2:
