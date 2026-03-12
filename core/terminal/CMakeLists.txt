# =============================================================================
# core/terminal/CMakeLists.txt — ve_terminal: interactive Node console
# =============================================================================

message(STATUS "")
message(STATUS "[ve_terminal]  interactive Node console")

add_executable(ve_terminal main.cpp)

target_link_libraries(ve_terminal PRIVATE ${VE_CORE_LIBRARY} ve_dep_json)

message(STATUS "")
