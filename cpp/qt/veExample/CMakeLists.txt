# =============================================================================
# cpp/qt/veExample/CMakeLists.txt — test executable, links against libveqt
# =============================================================================

ve_collect(${CMAKE_CURRENT_SOURCE_DIR} _EXAMPLE)

add_executable(veExample ${_EXAMPLE_ALL})

# Single dependency — libveqt provides all includes & Qt transitively
target_link_libraries(veExample PRIVATE ${VE_QT_LIBRARY})

# Qt: Widgets for QApplication (not transitively exported by libveqt)
ve_target_link_qt(veExample PRIVATE "Widgets")
