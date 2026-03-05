# =============================================================================
# cpp/qt/veQtBase/CMakeLists.txt — adds veQtBase sources to libveqt
#
# NOTE: core/ sources (base.cpp, platform/) are now built by libve.
#       libveqt links libve publicly, so consumers get ve/global.h etc.
# =============================================================================

ve_collect(${CMAKE_CURRENT_SOURCE_DIR} _QTCORE)

target_sources(${VE_QT_LIBRARY} PRIVATE ${_QTCORE_ALL})

# PUBLIC: veQtBase headers
target_include_directories(${VE_QT_LIBRARY} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
