# =============================================================================
# ve/program/CMakeLists.txt - ve.exe entry process + module DLLs
# =============================================================================

# --- ve.exe: minimal entry process ---
add_executable(ve main.cpp)
target_link_libraries(ve PRIVATE ${VE_CORE_LIBRARY})

add_custom_command(
    TARGET ve POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/ve.json
        $<TARGET_FILE_DIR:ve>/ve.json
    COMMENT "Copy default ve.json next to ve binary"
)

# --- Module DLLs ---
if(VE_BUILD_EXAMPLE)
    add_subdirectory(example)
endif()
