# =============================================================================
# ve/program/CMakeLists.txt - ve entry process + module DLLs
# =============================================================================
# Target must not be named "ve" because it conflicts with
# the core shared library target on Unix builds.

set(VE_PROGRAM_TARGET ve_program)

add_executable(${VE_PROGRAM_TARGET} main.cpp)
set_target_properties(${VE_PROGRAM_TARGET} PROPERTIES OUTPUT_NAME ve)
target_link_libraries(${VE_PROGRAM_TARGET} PRIVATE VersatileEngine::ve)

if(VE_INSTALL)
    install(TARGETS ${VE_PROGRAM_TARGET} RUNTIME DESTINATION bin)
endif()

# --- Module DLLs ---
if(VE_BUILD_MCP)
    add_subdirectory(mcp)
endif()
