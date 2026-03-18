# =============================================================================
# core/example/CMakeLists.txt — ve example programs (pure C++, no Qt)
# =============================================================================

message(STATUS "")
message(STATUS "[ve_example]  core example programs")

file(GLOB _EXAMPLE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/example_*.cpp)

foreach(_SRC ${_EXAMPLE_SOURCES})
    get_filename_component(_NAME ${_SRC} NAME_WE)
    add_executable(${_NAME} ${_SRC})
    target_link_libraries(${_NAME} PRIVATE ${VE_CORE_LIBRARY})
    if(UNIX)
        target_link_libraries(${_NAME} PRIVATE pthread dl)
    endif()
endforeach()

message(STATUS "")
