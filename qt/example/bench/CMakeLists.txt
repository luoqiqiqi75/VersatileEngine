# =============================================================================
# cpp/qt/example/bench/CMakeLists.txt — benchmark executable for imol::ModuleObject
# =============================================================================

ve_collect(${CMAKE_CURRENT_SOURCE_DIR} _BENCH)

list(FILTER _BENCH_ALL EXCLUDE REGEX "hashbench/")

add_executable(veBench ${_BENCH_ALL})

target_link_libraries(veBench PRIVATE ${VE_QT_LIBRARY})

