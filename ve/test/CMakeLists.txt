# =============================================================================
# core/test/CMakeLists.txt — ve_test executable (pure C++, no Qt)
# =============================================================================

message(STATUS "")
message(STATUS "[ve_test]  core unit tests")

file(GLOB_RECURSE _TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
list(FILTER _TEST_SOURCES EXCLUDE REGEX "/service/")

add_executable(ve_test ${_TEST_SOURCES})

target_include_directories(ve_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

option(VE_NODE_BENCH_LARGE "Enable extra-large JSON roundtrip benchmarks in ve_test (slow)" ON)
if(VE_NODE_BENCH_LARGE)
    target_compile_definitions(ve_test PRIVATE VE_NODE_BENCH_LARGE=1)
endif()

target_link_libraries(ve_test PRIVATE VersatileEngine::ve)

add_executable(ve_service_test service/test_connections.cpp ../src/asio_impl.cpp)
target_link_libraries(ve_service_test PRIVATE VersatileEngine::ve ve_dep_asio2)

if(UNIX AND NOT ANDROID)
    target_link_libraries(ve_test PRIVATE pthread dl)
elseif(ANDROID)
    target_link_libraries(ve_test PRIVATE dl)
endif()

message(STATUS "")
