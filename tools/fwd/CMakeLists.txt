# libfwd: port forwarding / tunnelling / routing library, plus the fwdd daemon
# and fwdctl client. Built libc-only (no QUIC); the control channel used by the
# in-framework `fwd` tool is plain UDP on loopback with a token. QUIC can be
# turned on later with -DFWD_ENABLE_QUIC=1 and linking OpenSSL >= 3.5.

file(GLOB fwd_SOURCES CONFIGURE_DEPENDS src/*.c)

add_library(fwd STATIC ${fwd_SOURCES})
add_library(fwd::fwd ALIAS fwd)

target_include_directories(fwd
  PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
set_target_properties(fwd PROPERTIES
  C_STANDARD 11
  C_EXTENSIONS ON
  POSITION_INDEPENDENT_CODE ON
  OUTPUT_NAME fwd
)
target_compile_options(fwd PRIVATE -Wall -Wextra)

# Daemon and client. The framework tool spawns the daemon; fwdctl is handy for
# manual control of a running fwdd.
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/tools/bin)
foreach(exe fwdd fwdctl)
  add_executable(${exe} examples/${exe}.c)
  target_link_libraries(${exe} PRIVATE fwd)
  set_target_properties(${exe} PROPERTIES
    C_STANDARD 11
    C_EXTENSIONS ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools/bin
  )
endforeach()

if(ENABLE_TESTS)
  enable_testing()
  add_executable(test_fwd tests/test_fwd.c)
  target_link_libraries(test_fwd PRIVATE fwd)
  target_include_directories(test_fwd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  set_target_properties(test_fwd PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
  add_test(NAME fwd COMMAND test_fwd)
endif()

# Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
