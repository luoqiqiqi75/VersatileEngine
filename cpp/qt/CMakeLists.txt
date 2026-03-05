# =============================================================================
# cpp/qt/CMakeLists.txt
# Qt module hub — builds a single libveqt from all enabled sub-modules
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

# --- Sub-module options ---
option(VE_BUILD_IMOL      "Build imol data tree"                   ON)
option(VE_BUILD_TERMINAL  "Build veTerminal (widget + TCP server)" ON)
option(VE_BUILD_QT_BASE   "Build veQtBase (ve::Data, ve::entry)"  ON)
option(VE_BUILD_SERVICE   "Build veService (networking)"           ON)
option(VE_BUILD_QML       "Build veQml (QML bridge)"               OFF)
option(VE_BUILD_EXAMPLE   "Build veExample (test executable)"      ON)

# --- Output library name ---
if(WIN32)
    set(VE_QT_LIBRARY libveqt)
else()
    set(VE_QT_LIBRARY veqt)
endif()

# ==========================================================
# Create the single shared library target (empty)
# Sub-modules add sources via target_sources()
# ==========================================================
add_library(${VE_QT_LIBRARY} SHARED)

# Single DLL export — everything uses VE_API from ve/global.h
target_compile_definitions(${VE_QT_LIBRARY} PRIVATE VE_LIBRARY)

# Qt link
ve_target_link_qt(${VE_QT_LIBRARY} PUBLIC  "Core;Network")
ve_target_link_qt(${VE_QT_LIBRARY} PRIVATE "Widgets")

# Link libve (core)
target_link_libraries(${VE_QT_LIBRARY} PUBLIC libve)

# ==========================================================
# Sub-modules
# ==========================================================
if(VE_BUILD_IMOL)
    add_subdirectory(imol)
    message(STATUS "  + imol")
endif()

if(VE_BUILD_QT_BASE)
    add_subdirectory(veQtBase)
    message(STATUS "  + veQtBase")
endif()

if(VE_BUILD_SERVICE)
    add_subdirectory(veService)
    message(STATUS "  + veService")
endif()

if(VE_BUILD_TERMINAL)
    add_subdirectory(veTerminal)
    message(STATUS "  + veTerminal")
endif()

if(VE_BUILD_QML)
    find_package(Qt${QT_VERSION_MAJOR} QUIET COMPONENTS Qml)
    if(Qt${QT_VERSION_MAJOR}Qml_FOUND)
        ve_target_link_qt(${VE_QT_LIBRARY} PUBLIC "Qml")
        add_subdirectory(veQml)
        message(STATUS "  + veQml")
    else()
        message(STATUS "  - veQml  (Qt${QT_VERSION_MAJOR}::Qml not found)")
    endif()
endif()

# --- Platform libs ---
if(UNIX AND NOT APPLE AND NOT ANDROID)
    target_link_libraries(${VE_QT_LIBRARY} PRIVATE dl)
endif()

# --- Example executable ---
if(VE_BUILD_EXAMPLE)
    add_subdirectory(veExample)
    message(STATUS "  + veExample (executable)")
endif()

message(STATUS "")
