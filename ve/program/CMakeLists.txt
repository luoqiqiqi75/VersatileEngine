# =============================================================================
# ve/program/CMakeLists.txt - ve entry process + module DLLs
# =============================================================================
# Target must not be named "ve" because it conflicts with
# the core shared library target on Unix builds.

set(VE_PROGRAM_TARGET ve_program)

add_executable(${VE_PROGRAM_TARGET} main.cpp)
set_target_properties(${VE_PROGRAM_TARGET} PROPERTIES OUTPUT_NAME ve)
target_link_libraries(${VE_PROGRAM_TARGET} PRIVATE VersatileEngine::ve)

add_custom_command(
    TARGET ${VE_PROGRAM_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/ve.json
        $<TARGET_FILE_DIR:${VE_PROGRAM_TARGET}>/ve.json
    COMMENT "Copy default ve.json next to ve binary"
)

# --- Module DLLs ---
if(VE_BUILD_MCP)
    add_subdirectory(mcp)
endif()
