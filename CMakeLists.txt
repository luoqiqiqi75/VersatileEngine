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

# --- Local overrides (gitignored, per-developer) — before ve_compile so e.g. VE_FORCE_DEBUG_INFO applies ---
file(GLOB VE_LOCAL_CMAKE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/cmake/_*.cmake")
foreach(VE_LOCAL_CMAKE_FILE ${VE_LOCAL_CMAKE_FILES})
    include(${VE_LOCAL_CMAKE_FILE})
    get_filename_component(_fname ${VE_LOCAL_CMAKE_FILE} NAME)
    message(STATUS "Loaded local config: cmake/${_fname}")
endforeach()

include(${VE_NAMESPACE}_compile)
include(${VE_NAMESPACE}_common)

# --- Bundled dependencies (header-only, always available) ---
include(${VE_NAMESPACE}_deps)

message(STATUS "")
message(STATUS "BUILD_SHARED_LIBS = ${BUILD_SHARED_LIBS}")

# ==========================================================================
# Install option (must be set before subdirs that use it)
# ==========================================================================
option(VE_INSTALL "Install VE library, headers and CMake config" OFF)

# ==========================================================================
# ve/ - pure C++ base library -> libve
# ==========================================================================
add_subdirectory(ve)

# ==========================================================================
# Test option - build core unit tests (pure C++, no dependencies beyond libve)
# ==========================================================================
option(VE_BUILD_TEST     "Build core unit tests (ve_test)"              ON)
option(VE_BUILD_EXAMPLE  "Build example module DLLs"                    ON)

if(VE_BUILD_TEST)
    add_subdirectory(ve/test)
endif()

# ve.exe entry process + module DLLs (example, etc.)
add_subdirectory(ve/program)

# ==========================================================================
# Module options - all default ON; each module's CMakeLists.txt checks its
# own dependencies and returns early if not satisfied.
# ==========================================================================
option(VE_BUILD_QT  "Build qt/  -> libveqt  (needs Qt5 or Qt6)"                    ON)
option(VE_BUILD_DDS "Build ros/ -> libvedds  (needs FastDDS: fastrtps + fastcdr)"   ON)
option(VE_BUILD_RTT "Build rtt/ -> libvertt  (pure C++ IMOL xcore)"                ON)

if(VE_BUILD_QT)
    add_subdirectory(qt)
endif()

if(VE_BUILD_DDS)
    add_subdirectory(ros)
endif()

if(VE_BUILD_RTT)
    add_subdirectory(rtt)
endif()

# ==========================================================================
# Install (VE_INSTALL defaults OFF for add_subdirectory; set ON to install VE)
# ==========================================================================
if(VE_INSTALL)
    include(cmake/ve_install.cmake)
endif()

message(STATUS "")
message(STATUS "${VE_NAME} CMake End    ========================")
message(STATUS "")
