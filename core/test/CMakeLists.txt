# =============================================================================
# core/test/CMakeLists.txt — ve_test executable (pure C++, no Qt)
# =============================================================================

message(STATUS "")
message(STATUS "[ve_test]  core unit tests")

file(GLOB_RECURSE _TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(ve_test ${_TEST_SOURCES})

target_include_directories(ve_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(ve_test PRIVATE ${VE_CORE_LIBRARY})

if(UNIX)
    target_link_libraries(ve_test PRIVATE pthread dl)
endif()

message(STATUS "")
