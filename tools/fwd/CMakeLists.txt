# libfwd: port forwarding / tunnelling / routing library, plus the fwdd daemon
# and fwdctl client. Compile-time switches live in config.yaml and are emitted
# into the generated include/fwd_config.hpp (see fwd_config.h). The control
# channel used by the in-framework `fwd` tool is plain UDP on loopback with a
# token; QUIC is off by default.

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

# QUIC is selected in config.yaml (CONFIG_FWD_ENABLE_QUIC in the generated
# header). When on, the QUIC/TLS sources need OpenSSL >= 3.5 linked in; read
# the switch from the generated header so the YAML stays the single source.
set(_fwd_config_hpp ${CMAKE_CURRENT_SOURCE_DIR}/include/fwd_config.hpp)
set(_fwd_quic OFF)
if(EXISTS ${_fwd_config_hpp})
  file(READ ${_fwd_config_hpp} _fwd_config_text)
  string(REGEX MATCH "#define[ \t]+CONFIG_FWD_ENABLE_QUIC[ \t]+1" _fwd_quic_match "${_fwd_config_text}")
  if(_fwd_quic_match)
    set(_fwd_quic ON)
  endif()
endif()
if(_fwd_quic)
  find_package(OpenSSL 3.5 REQUIRED)
  target_link_libraries(fwd PUBLIC OpenSSL::SSL OpenSSL::Crypto)
  message(STATUS "libfwd: QUIC enabled (OpenSSL ${OPENSSL_VERSION})")
endif()

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
