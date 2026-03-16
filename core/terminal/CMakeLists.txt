# =============================================================================
# core/terminal/CMakeLists.txt — ve_terminal: interactive Node console
# =============================================================================

message(STATUS "")
message(STATUS "[ve_terminal]  interactive Node console")

ve_find_package(simdjson
    GIT_REPO  https://github.com/simdjson/simdjson.git
    GIT_TAG   v3.10.1
    OPTIONS
        SIMDJSON_DEVELOPER_MODE OFF
)

add_executable(ve_terminal main.cpp)

target_link_libraries(ve_terminal PRIVATE ${VE_CORE_LIBRARY} simdjson::simdjson)

message(STATUS "")
