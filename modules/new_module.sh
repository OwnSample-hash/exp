#!/bin/bash
# This script creates a new module directory with a basic structure.
#
# Usage: ./new_module.sh <module_name> --build-type=dynamic,static
#

if [ "$#" -ne 1 ]; then
  echo "Usage: $0 <module_name>"
  exit 1
fi
MODULE_NAME=$1
MODULE_DIR="./modules/$MODULE_NAME"

if [ -d "$MODULE_DIR" ]; then
  echo "Module '$MODULE_NAME' already exists."
  exit 1
fi

mkdir -p "$MODULE_DIR"/src
echo "# $MODULE_NAME Module" > "$MODULE_DIR/README.md"
echo "Created module directory at '$MODULE_DIR' with README.md"

MODULE_CMAKE_FILE="$MODULE_DIR/CMakeLists.txt"

if [ "$#" -eq 2 ]; then
  IFS=',' read -r -a BUILD_TYPES <<< "${2#--build-type=}"
else
  BUILD_TYPES=("dynamic" "static")
fi
echo "Creating CMakeLists.txt for build types: ${BUILD_TYPES[*]}"
for BUILD_TYPE in "${BUILD_TYPES[@]}"; do
  if [[ "$BUILD_TYPE" != "dynamic" && "$BUILD_TYPE" != "static" ]]; then
    echo "Unsupported build type: $BUILD_TYPE. Supported types are 'dynamic' and 'static'."
    exit 1
  fi
  echo "options(MODULE_PLATFORM_${BUILD_TYPE^^} \"Build ${MODULE_NAME} as ${BUILD_TYPE} library\" ON)" >> "$MODULE_CMAKE_FILE"
done


for BUILD_TYPE in "${BUILD_TYPES[@]}"; do
  echo "if(MODULE_PLATFORM_${BUILD_TYPE^^})" >> "$MODULE_CMAKE_FILE"
  echo "  add_library(${MODULE_NAME}_${BUILD_TYPE} ${BUILD_TYPE} src/${MODULE_NAME}.cpp)" >> "$MODULE_CMAKE_FILE"
  echo "  target_include_directories(${MODULE_NAME}_${BUILD_TYPE} PUBLIC \${CMAKE_CURRENT_SOURCE_DIR}/src)" >> "$MODULE_CMAKE_FILE"
  echo "  set_target_properties(${MODULE_NAME}_${BUILD_TYPE} PROPERTIES OUTPUT_NAME \"${MODULE_NAME}\")" >> "$MODULE_CMAKE_FILE"
  echo "endif()" >> "$MODULE_CMAKE_FILE"
  echo "" >> "$MODULE_CMAKE_FILE"
done
# Vim: set expandtab tabstop=2 shiftwidth=2:
