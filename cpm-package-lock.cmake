# CPM Package Lock
# This file should be committed to version control

# spdlog
CPMDeclarePackage(spdlog
  VERSION 1.17.0
  GITHUB_REPOSITORY gabime/spdlog
  SYSTEM YES
  EXCLUDE_FROM_ALL YES
)
# json
CPMDeclarePackage(json
  VERSION 3.12.0
  GITHUB_REPOSITORY nlohmann/json
  SYSTEM YES
  EXCLUDE_FROM_ALL YES
)
# yaml-cpp (unversioned)
# CPMDeclarePackage(yaml-cpp
#  GIT_TAG yaml-cpp-0.9.0
#  GITHUB_REPOSITORY jbeder/yaml-cpp
#  SYSTEM YES
#  EXCLUDE_FROM_ALL YES
#)
# Catch2
CPMDeclarePackage(Catch2
  VERSION 3.15.3
  GITHUB_REPOSITORY catchorg/Catch2
  SYSTEM YES
  EXCLUDE_FROM_ALL YES
)
# lua
CPMDeclarePackage(lua
  NAME lua
  VERSION 5.5.1
  DOWNLOAD_ONLY YES
  GIT_REPOSITORY https://github.com/lua/lua.git
)
# cpp-httplib
CPMDeclarePackage(cpp-httplib
  VERSION 0.46.1
  GITHUB_REPOSITORY yhirose/cpp-httplib
  SYSTEM YES
  EXCLUDE_FROM_ALL YES
)
