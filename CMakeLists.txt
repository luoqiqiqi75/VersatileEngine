cmake_minimum_required(VERSION 3.15)

set(VE_NAME VersatileEngine)
set(VE_NAMESPACE ve)

project(${VE_NAME} VERSION 2.0.0 LANGUAGES CXX)

message(STATUS "")
message(STATUS "${VE_NAME} CMake Begin  ========================")
message(STATUS "${VE_NAME} Version: ${PROJECT_VERSION}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# --- CMake module path ---
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(PreventInBuildInstalls)
include(PreventInSourceBuilds)
include(${VE_NAMESPACE}_compile)
include(${VE_NAMESPACE}_common)

# --- Local overrides (gitignored, per-developer) ---
file(GLOB VE_LOCAL_CMAKE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/cmake/_*.cmake")
foreach(VE_LOCAL_CMAKE_FILE ${VE_LOCAL_CMAKE_FILES})
    include(${VE_LOCAL_CMAKE_FILE})
    get_filename_component(_fname ${VE_LOCAL_CMAKE_FILE} NAME)
    message(STATUS "Loaded local config: cmake/${_fname}")
endforeach()

# --- Bundled dependencies (header-only, always available) ---
include(${VE_NAMESPACE}_deps)

message(STATUS "")
message(STATUS "BUILD_SHARED_LIBS = ${BUILD_SHARED_LIBS}")

# ==========================================================================
# core/ — pure C++ base library -> libve
# ==========================================================================
add_subdirectory(core)

# ==========================================================================
# Test option — build core unit tests (pure C++, no dependencies beyond libve)
# ==========================================================================
option(VE_BUILD_TEST "Build core unit tests (ve_test)" OFF)
option(VE_BUILD_TERMINAL "Build interactive Node console (ve_terminal)" OFF)

if(VE_BUILD_TEST)
    add_subdirectory(core/test)
endif()

if(VE_BUILD_TERMINAL)
    add_subdirectory(core/terminal)
endif()

# ==========================================================================
# Module options — all default ON; each module's CMakeLists.txt checks its
# own dependencies and returns early if not satisfied.
# ==========================================================================
option(VE_BUILD_QT  "Build cpp/qt  -> libveqt  (needs Qt5 or Qt6)"              ON)
option(VE_BUILD_ROS "Build cpp/ros -> libveros  (needs catkin or ament_cmake)"   ON)
option(VE_BUILD_RTT "Build cpp/rtt -> libvertt  (pure C++ IMOL xcore)"           ON)

if(VE_BUILD_QT)
    add_subdirectory(cpp/qt)
endif()

if(VE_BUILD_ROS)
    add_subdirectory(cpp/ros)
endif()

if(VE_BUILD_RTT)
    add_subdirectory(cpp/rtt)
endif()

message(STATUS "")
message(STATUS "${VE_NAME} CMake End    ========================")
message(STATUS "")
