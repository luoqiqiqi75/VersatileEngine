# =============================================================================
# cpp/qt/veTerminal/CMakeLists.txt — adds terminal sources to libveqt
# =============================================================================

set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/src)

ve_collect(${CMAKE_CURRENT_SOURCE_DIR} _TERMINAL)

target_sources(${VE_QT_LIBRARY} PRIVATE ${_TERMINAL_ALL})

# PUBLIC: consumers include "ve/core/terminal.h"
target_include_directories(${VE_QT_LIBRARY} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# PRIVATE: internal src headers, UIC output directory
target_include_directories(${VE_QT_LIBRARY} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
)
