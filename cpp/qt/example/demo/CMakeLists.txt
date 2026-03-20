# =============================================================================
# cpp/qt/example/demo/CMakeLists.txt — demo executable, links against libveqt
# =============================================================================

ve_collect(${CMAKE_CURRENT_SOURCE_DIR} _EXAMPLE)

add_executable(veExample ${_EXAMPLE_ALL})

target_link_libraries(veExample PRIVATE ${VE_QT_LIBRARY})

ve_target_link_qt(veExample PRIVATE "Widgets")

add_custom_command(
    TARGET veExample POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/example.json
        $<TARGET_FILE_DIR:veExample>/example.json
    COMMENT "Copy example.json next to veExample"
)
