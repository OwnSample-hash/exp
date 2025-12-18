# CPM Package Lock
# This file should be committed to version control

# spdlog
CPMDeclarePackage(spdlog
  VERSION 1.16.0
  GITHUB_REPOSITORY gabime/spdlog
  SYSTEM YES
  EXCLUDE_FROM_ALL YES
)
# Catch2
CPMDeclarePackage(Catch2
  VERSION 3.11.0
  GITHUB_REPOSITORY catchorg/Catch2
  SYSTEM YES
  EXCLUDE_FROM_ALL YES
)
