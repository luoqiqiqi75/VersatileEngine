# =============================================================================
# ve/program/example/CMakeLists.txt - ve.example module DLL
# =============================================================================

add_library(ve_example SHARED
    ExampleModule.cpp
    ExampleModule.h
)

target_link_libraries(ve_example PRIVATE ${VE_CORE_LIBRARY})
