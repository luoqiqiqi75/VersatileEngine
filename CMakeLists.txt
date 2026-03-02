cmake_minimum_required(VERSION 3.15)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(VE_NAME VersatileEngine)
set(VE_NAMESPACE ve)

project(${VE_NAME} VERSION 1.0.0 LANGUAGES CXX)

message(STATUS "")
message(STATUS "${VE_NAME} CMake Begin  ------------------------")
message(STATUS "${VE_NAME} Version: ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# --- Output directories ---
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib" CACHE PATH "Archive output dir.")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib" CACHE PATH "Library output dir.")
set(CMAKE_PDB_OUTPUT_DIRECTORY     "${CMAKE_BINARY_DIR}/bin" CACHE PATH "PDB (MSVC debug symbol) output dir.")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" CACHE PATH "Executable/dll output dir.")

# --- CMake module path ---
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(PreventInBuildInstalls)
include(PreventInSourceBuilds)
include(${VE_NAMESPACE}_common)

# --- Local overrides (gitignored, per-developer) ---
# Any cmake/_*.cmake files are auto-loaded and gitignored.
# See cmake/_local.cmake.example for available options.
file(GLOB VE_LOCAL_CMAKE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/cmake/_*.cmake")
foreach(VE_LOCAL_CMAKE_FILE ${VE_LOCAL_CMAKE_FILES})
    include(${VE_LOCAL_CMAKE_FILE})
    get_filename_component(_fname ${VE_LOCAL_CMAKE_FILE} NAME)
    message(STATUS "Loaded local config: cmake/${_fname}")
endforeach()

# --- Build options ---
if(NOT DEFINED BUILD_SHARED_LIBS)
    set(BUILD_SHARED_LIBS ON CACHE BOOL "Build shared libraries")
endif()

set(CMAKE_INCLUDE_CURRENT_DIR ON)

# --- VE options ---
option(BUILD_CORE    "Build ${VE_NAME} Qt-based Core library" ON)
option(BUILD_SERVICE "Build ${VE_NAME} Service library"       ON)
option(BUILD_MAIN    "Build ${VE_NAME} Main program"          ON)

if(LOG_MIN_FILE_LEVEL)
    add_definitions("-DLOG_MIN_FILE_LEVEL=${LOG_MIN_FILE_LEVEL}")
endif()

# --- 3rdparty dependencies (bundled, header-only) ---
include(${VE_NAMESPACE}_3rdparty)

# --- Find Qt (shared across all targets, supports Qt5 and Qt6) ---
set(VE_QT_COMPONENTS Core Network Widgets)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS ${VE_QT_COMPONENTS})
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS ${VE_QT_COMPONENTS})
message(STATUS "Found Qt${QT_VERSION_MAJOR}: ${Qt${QT_VERSION_MAJOR}_VERSION}")

# --- Print configuration summary ---
message(STATUS "")
message(STATUS "Configuration:")
message(STATUS "  BUILD_CORE        = ${BUILD_CORE}")
message(STATUS "  BUILD_SERVICE     = ${BUILD_SERVICE}")
message(STATUS "  BUILD_MAIN        = ${BUILD_MAIN}")
message(STATUS "  BUILD_SHARED_LIBS = ${BUILD_SHARED_LIBS}")
message(STATUS "")

# --- Subdirectories ---

# core
if(BUILD_CORE)
    ve_add_library(core VE_CORE_LIBRARY VE_CORE_LIBRARY_INCLUDE)
endif()

# service
if(BUILD_SERVICE)
    ve_add_library(service VE_SERVICE_LIBRARY VE_SERVICE_LIBRARY_INCLUDE)
endif()

# main
if(BUILD_MAIN)
    add_subdirectory(main)
endif()

message(STATUS "${VE_NAME} CMake End    ------------------------")
message(STATUS "")