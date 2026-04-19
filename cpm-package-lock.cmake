# CPM Package Lock
# This file should be committed to version control

# spdlog
CPMDeclarePackage(spdlog
  VERSION 1.17.0
  GITHUB_REPOSITORY gabime/spdlog
  SYSTEM YES
  EXCLUDE_FROM_ALL YES
)
# lua
CPMDeclarePackage(lua
  NAME lua
  VERSION 5.5.0
  DOWNLOAD_ONLY YES
  GIT_REPOSITORY https://github.com/lua/lua.git
)
