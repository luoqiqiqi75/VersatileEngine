# =============================================================================
# cpp/qt/veBench/CMakeLists.txt — benchmark executable for imol::ModuleObject
# Mirrors core/test/test_node_bench.cpp for performance comparison with ve::Node
# =============================================================================

ve_collect(${CMAKE_CURRENT_SOURCE_DIR} _BENCH)

# Exclude hashbench/ from the main veBench sources (it has its own target)
list(FILTER _BENCH_ALL EXCLUDE REGEX "hashbench/")

add_executable(veBench ${_BENCH_ALL})

# Link libveqt (provides imol, Qt transitively)
target_link_libraries(veBench PRIVATE ${VE_QT_LIBRARY})

# =============================================================================
# hashbench — standalone hash container benchmark (temporary)
# =============================================================================
file(GLOB_RECURSE _HASHBENCH_SRC ${CMAKE_CURRENT_SOURCE_DIR}/hashbench/*.cpp)

add_executable(hashbench ${_HASHBENCH_SRC})

# libveqt provides: Qt::Core (transitively), libve (core headers for ordered_hashmap.h)
target_link_libraries(hashbench PRIVATE ${VE_QT_LIBRARY})
