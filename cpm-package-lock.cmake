# CPM Package Lock
# This file should be committed to version control

# spdlog
CPMDeclarePackage(spdlog
  NAME spdlog
  URL
    "https://github.com/gabime/spdlog/archive/refs/tags/v1.17.0.tar.gz"
    "URL_HASH"
    "SHA256=d8862955c6d74e5846b3f580b1605d2428b11d97a410d86e2fb13e857cd3a744"
    "PATCHES"
    "patches/spdlog.patch"
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
