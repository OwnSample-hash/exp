if(FWD_ENABLE)
  set(MOD_NAME fwd)
  set(MOD_SRC src/fwd.cpp)

  # The shared module links libfwd (the static `fwd` target from tools/fwd), so
  # the plugin target cannot also be named `fwd`; build it as `fwd_mod` and give
  # the artifact the plain `fwd.so` name the loader expects.
  add_library(fwd_mod SHARED ${MOD_SRC})
  target_include_directories(fwd_mod PUBLIC include ../../include/)
  target_link_libraries(fwd_mod PRIVATE spdlog::spdlog fwd::fwd)

  execute_process(COMMAND uuidgen OUTPUT_VARIABLE ${MOD_NAME}_UUID OUTPUT_STRIP_TRAILING_WHITESPACE)
  target_compile_definitions(fwd_mod PRIVATE
    MODULE_NAME="${MOD_NAME}"
    FWD_UUID="${${MOD_NAME}_UUID}"
    FWDD_PATH="$<TARGET_FILE:fwdd>" # absolute path to the daemon the tool spawns
  )
  set_target_properties(fwd_mod PROPERTIES OUTPUT_NAME ${MOD_NAME} PREFIX "")

  # Drop the built module where the plugin loader scans (build/plugins/).
  add_custom_command(TARGET fwd_mod POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE:fwd_mod> ${CMAKE_CURRENT_BINARY_DIR}/../$<TARGET_FILE_NAME:fwd_mod>
  )
endif()
# Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
