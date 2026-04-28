# =============================================================================
# qt/CMakeLists.txt
# VE + Qt - builds libveqt from include/ + src/, plus module DLLs in program/
#
# Self-checks: probes for Qt5/Qt6, returns early if not found.
# =============================================================================

# --- Probe for Qt ---
find_package(QT NAMES Qt6 Qt5 QUIET COMPONENTS Core)
if(NOT QT_FOUND)
    message(STATUS "")
    message(STATUS "[SKIP] libveqt — Qt5/Qt6 not found")
    message(STATUS "       Set CMAKE_PREFIX_PATH to your Qt installation to enable.")
    message(STATUS "")
    return()
endif()

set(VE_QT_COMPONENTS Core Network Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS ${VE_QT_COMPONENTS})

message(STATUS "")
message(STATUS "[libveqt]  Qt${QT_VERSION_MAJOR} ${Qt${QT_VERSION_MAJOR}_VERSION}")

# --- Qt auto-processors ---
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# --- Build options ---
option(VE_BUILD_QT_QML     "Build QML bridge (needs Qt Qml)"              ON)
option(VE_BUILD_QT_BROWSER "Build veqtbrowser module DLL (WebView)"       ON)

# --- Output library name ---
ve_resolve_library_target_name(VE_QT_LIBRARY veqt)

# ==========================================================
# Create the single library target (SHARED on desktop, STATIC on iOS)
# ==========================================================
add_library(${VE_QT_LIBRARY})

target_compile_definitions(${VE_QT_LIBRARY} PRIVATE VE_LIBRARY VE_QT_HAS_IMOL=1)

# Qt link
ve_target_link_qt(${VE_QT_LIBRARY} PUBLIC  "Core;Network")
ve_target_link_qt(${VE_QT_LIBRARY} PRIVATE "Widgets")

# Link core VE target
target_link_libraries(${VE_QT_LIBRARY} PUBLIC VersatileEngine::ve)

# ==========================================================
# Public headers — single unified include/
# ==========================================================
target_include_directories(${VE_QT_LIBRARY} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ==========================================================
# QML bridge probe (before ve_collect to decide source filtering)
# ==========================================================
set(_VE_QT_HAS_QML OFF)
if(VE_BUILD_QT_QML OR VE_BUILD_QT_BROWSER)
    find_package(Qt${QT_VERSION_MAJOR} QUIET COMPONENTS Qml)
    if(TARGET Qt${QT_VERSION_MAJOR}::Qml)
        set(_VE_QT_HAS_QML ON)
    endif()
endif()

# ==========================================================
# Library sources
# ==========================================================
ve_collect(${CMAKE_CURRENT_SOURCE_DIR} _QT)

# quick_node.cpp is core Qt bridge (no Qml dependency), always built.
# qml_register.cpp and qt_launch_module.cpp require Qt::Qml, filter them out if not available.
if(NOT _VE_QT_HAS_QML)
    list(FILTER _QT_ALL EXCLUDE REGEX ".*/src/qml/(qml_register|qt_launch_module)\\.cpp$")
endif()

target_sources(${VE_QT_LIBRARY} PRIVATE ${_QT_ALL})

# PRIVATE: internal src headers, AUTOUIC output
target_include_directories(${VE_QT_LIBRARY} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/terminal
    ${CMAKE_CURRENT_SOURCE_DIR}/src/service
    ${CMAKE_CURRENT_BINARY_DIR}
)

set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/src/terminal)

# deps: asio2 for service layer
target_link_libraries(${VE_QT_LIBRARY} PRIVATE ve_dep_asio2)

if(MSVC)
    target_compile_options(${VE_QT_LIBRARY} PRIVATE /bigobj)
endif()

# --- Platform libs ---
if(UNIX AND NOT APPLE AND NOT ANDROID)
    target_link_libraries(${VE_QT_LIBRARY} PRIVATE dl)
endif()

# ==========================================================
# QML bridge (conditional)
# ==========================================================
if(_VE_QT_HAS_QML)
    ve_target_link_qt(${VE_QT_LIBRARY} PUBLIC "Qml")
    message(STATUS "  + QML bridge")
elseif(VE_BUILD_QT_QML)
    message(STATUS "  - Skip QML bridge (Qt${QT_VERSION_MAJOR} Qml not found)")
endif()

# ==========================================================
# Module DLLs (loaded at runtime by ve.exe)
# ==========================================================
if(VE_BUILD_QT_BROWSER)
    if(NOT _VE_QT_HAS_QML)
        message(STATUS "  - Skip veqtbrowser (requires Qt${QT_VERSION_MAJOR} Qml)")
    else()
        find_package(Qt${QT_VERSION_MAJOR} QUIET COMPONENTS Quick WebView)
        if(TARGET Qt${QT_VERSION_MAJOR}::Quick AND TARGET Qt${QT_VERSION_MAJOR}::WebView)
            add_subdirectory(program/browser)
            message(STATUS "  + veqtbrowser (module DLL)")
        else()
            message(STATUS "  - Skip veqtbrowser (Qt${QT_VERSION_MAJOR} Quick/WebView not found)")
        endif()
    endif()
endif()

message(STATUS "")

add_subdirectory(example/bench)
