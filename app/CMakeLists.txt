file(GLOB_RECURSE main_app_SOURCE src/*.cpp)

add_executable(main_app
  ${main_app_SOURCE}
)

if (${TLS_BACKEND} STREQUAL "openssl")
  find_package(OpenSSL COMPONENTS SSL)
  target_link_libraries(main_app PRIVATE OpenSSL::SSL)
elseif(${TLS_BACKEND} STREQUAL "mbedtls")
  option(ENABLE_PROGRAMS OFF)
  message(STATUS "MbedTLS implementation is partial")
  CPMAddPackage("Mbed-TLS/mbedtls#v4.2.0")
  target_link_libraries(main_app PRIVATE
    MbedTLS::mbedtls
    MbedTLS::tfpsacrypto
    MbedTLS::mbedx509
  )
endif()

target_link_libraries(main_app PRIVATE spdlog::spdlog)
target_include_directories(main_app PRIVATE ../include ./include)
set_target_properties(main_app PROPERTIES POSITION_INDEPENDENT_CODE ON)

set_property(TARGET main_app PROPERTY ENABLE_EXPORTS ON)

string(TOLOWER ${CMAKE_SYSTEM_NAME} system_name)

file(GLOB_RECURSE impl lib/${system_name}/*.cpp)

message(STATUS "TLS_BACKEND: ${TLS_BACKEND}")

target_sources(main_app PRIVATE ${impl} ${CMAKE_CURRENT_SOURCE_DIR}/lib/tls/${TLS_BACKEND}.cpp)

add_pch(main_app)

# Vim: set expandtab tabstop=2 shiftwidth=2:
