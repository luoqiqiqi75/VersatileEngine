# =============================================================================
# core/test/CMakeLists.txt — ve_test executable (pure C++, no Qt)
# =============================================================================

message(STATUS "")
message(STATUS "[ve_test]  core unit tests")

file(GLOB_RECURSE _TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
list(FILTER _TEST_SOURCES EXCLUDE REGEX "/service/")
list(FILTER _TEST_SOURCES EXCLUDE REGEX "/runtime/")

add_executable(ve_test ${_TEST_SOURCES})

target_include_directories(ve_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

option(VE_NODE_BENCH_LARGE "Enable extra-large JSON roundtrip benchmarks in ve_test (slow)" ON)
if(VE_NODE_BENCH_LARGE)
    target_compile_definitions(ve_test PRIVATE VE_NODE_BENCH_LARGE=1)
endif()

target_link_libraries(ve_test PRIVATE VersatileEngine::ve)

# Isolate startup configuration and default-pool lifecycle from other tests.
add_executable(ve_runtime_config_test runtime/test_runtime_config.cpp main.cpp)
target_include_directories(ve_runtime_config_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ve_runtime_config_test PRIVATE VersatileEngine::ve)
target_compile_definitions(ve_runtime_config_test PRIVATE VE_CONFIG_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../program")
if(NOT "${VE_LOOP_POOL_THREADS}" STREQUAL "")
    target_compile_definitions(ve_runtime_config_test PRIVATE VE_LOOP_POOL_THREADS=${VE_LOOP_POOL_THREADS})
endif()

add_executable(ve_log_bench EXCLUDE_FROM_ALL runtime/bench_log.cpp)
target_link_libraries(ve_log_bench PRIVATE VersatileEngine::ve)

add_executable(ve_service_test service/test_connections.cpp ../src/asio_impl.cpp)
target_link_libraries(ve_service_test PRIVATE VersatileEngine::ve ve_dep_asio2)

if(UNIX AND NOT ANDROID)
    target_link_libraries(ve_test PRIVATE pthread dl)
    target_link_libraries(ve_runtime_config_test PRIVATE pthread)
    target_link_libraries(ve_log_bench PRIVATE pthread)
elseif(ANDROID)
    target_link_libraries(ve_test PRIVATE dl)
endif()

message(STATUS "")
