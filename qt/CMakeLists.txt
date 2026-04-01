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
if(WIN32)
    set(VE_QT_LIBRARY libveqt)
else()
    set(VE_QT_LIBRARY veqt)
endif()

# ==========================================================
# Create the single shared library target
# ==========================================================
add_library(${VE_QT_LIBRARY} SHARED)

target_compile_definitions(${VE_QT_LIBRARY} PRIVATE VE_LIBRARY VE_QT_HAS_IMOL=1)

# Qt link
ve_target_link_qt(${VE_QT_LIBRARY} PUBLIC  "Core;Network")
ve_target_link_qt(${VE_QT_LIBRARY} PRIVATE "Widgets")

# Link libve (core)
target_link_libraries(${VE_QT_LIBRARY} PUBLIC libve)

# ==========================================================
# Public headers — single unified include/
# ==========================================================
target_include_directories(${VE_QT_LIBRARY} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ==========================================================
# Library sources
# ==========================================================
file(GLOB_RECURSE _IMOL_SRC     src/imol/*.cpp)
file(GLOB_RECURSE _BASE_SRC     src/base/*.cpp)
file(GLOB_RECURSE _TERMINAL_SRC src/terminal/*.cpp src/terminal/*.h src/terminal/*.ui)
file(GLOB_RECURSE _SERVICE_SRC  src/service/*.cpp  src/service/*.h)
file(GLOB_RECURSE _RES          res/*.qrc)
file(GLOB_RECURSE _INCLUDES     include/*.h)

target_sources(${VE_QT_LIBRARY} PRIVATE
    ${_IMOL_SRC} ${_BASE_SRC} ${_TERMINAL_SRC} ${_SERVICE_SRC}
    ${_RES} ${_INCLUDES}
)

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
if(VE_BUILD_QT_QML OR VE_BUILD_QT_BROWSER)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Qml)
    ve_target_link_qt(${VE_QT_LIBRARY} PUBLIC "Qml")
    file(GLOB_RECURSE _QML_SRC src/qml/*.cpp)
    file(GLOB_RECURSE _QML_HDR include/ve/qt/qml/*.h)
    target_sources(${VE_QT_LIBRARY} PRIVATE ${_QML_SRC} ${_QML_HDR})
    message(STATUS "  + QML bridge")
endif()

# ==========================================================
# Module DLLs (loaded at runtime by ve.exe)
# ==========================================================
if(VE_BUILD_QT_BROWSER)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Quick WebView)
    add_subdirectory(program/browser)
    message(STATUS "  + veqtbrowser (module DLL)")
endif()

message(STATUS "")

add_subdirectory(example/bench)
