# =============================================================================
# ve/program/CMakeLists.txt - ve entry process + module DLLs
# =============================================================================
# Target must not be named "ve" — conflicts with libve shared library target "ve".

add_executable(ve_program main.cpp)
set_target_properties(ve_program PROPERTIES OUTPUT_NAME ve)
target_link_libraries(ve_program PRIVATE ${VE_CORE_LIBRARY})

add_custom_command(
    TARGET ve_program POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/ve.json
        $<TARGET_FILE_DIR:ve_program>/ve.json
    COMMENT "Copy default ve.json next to ve binary"
)

# --- Module DLLs ---
if(VE_BUILD_MCP)
    add_subdirectory(mcp)
endif()
