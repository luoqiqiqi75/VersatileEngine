# =============================================================================
# cpp/qt/imol/CMakeLists.txt — adds imol sources to libveqt
# =============================================================================

ve_collect(${CMAKE_CURRENT_SOURCE_DIR} _IMOL)

target_sources(${VE_QT_LIBRARY} PRIVATE ${_IMOL_ALL})

# PUBLIC: consumers include "imol/modulemanager.h"
target_include_directories(${VE_QT_LIBRARY} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
