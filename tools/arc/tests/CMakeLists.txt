enable_testing()
set(TOOL_NAME arc_test)
set(TOOL_SRC src/arc.cpp)
add_executable(${TOOL_NAME} ${TOOL_SRC})
target_link_libraries(${TOOL_NAME} PRIVATE Catch2::Catch2WithMain arc::arc)
target_include_directories(${TOOL_NAME} PRIVATE ../include/ ../../../include/)

if(${ARC_FUSE_ENABLE})
target_compile_definitions(${TOOL_NAME} PRIVATE FUSE_USE_VERSION=${FUSE_USE_VERSION})
endif()

add_test(NAME ${TOOL_NAME} COMMAND ${TOOL_NAME})
