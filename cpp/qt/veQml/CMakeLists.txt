# =============================================================================
# cpp/qt/veQml/CMakeLists.txt — adds QML bridge sources to libveqt
# =============================================================================

ve_collect(${CMAKE_CURRENT_SOURCE_DIR} _QML)

target_sources(${VE_QT_LIBRARY} PRIVATE ${_QML_ALL})

target_include_directories(${VE_QT_LIBRARY} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
