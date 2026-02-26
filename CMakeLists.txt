cmake_minimum_required(VERSION 3.15)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(VE_NAME VersatileEngine)
set(VE_NAMESPACE ve)

project(${VE_NAME} VERSION 1.0.0 LANGUAGES CXX)

message(STATUS "${VE_NAME} CMake Begin  ------------------------")
message(STATUS "${VE_NAME} Version: ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")

# compile
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib" CACHE PATH "Archive output dir.")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib" CACHE PATH "Library output dir.")
set(CMAKE_PDB_OUTPUT_DIRECTORY     "${CMAKE_BINARY_DIR}/bin" CACHE PATH "PDB (MSVC debug symbol)output dir.")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" CACHE PATH "Executable/dll output dir.")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(PreventInBuildInstalls)
include(PreventInSourceBuilds)
include(${VE_NAMESPACE}_common)

if(NOT DEFINED BUILD_SHARED_LIBS)
    set(BUILD_SHARED_LIBS ON CACHE BOOL "Build shared library")
endif()

#set(CMAKE_SHARED_LIBRARY_PREFIX "")
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# todo
if (LOG_MIN_FILE_LEVEL)
    add_definitions("-DLOG_MIN_FILE_LEVEL=${LOG_MIN_FILE_LEVEL}")
endif ()

# core
option(BUILD_CORE "Build ${VE_NAME} Qt based Core library" ON)
if (BUILD_CORE)
    ve_add_library(core VE_CORE_LIBRARY VE_CORE_LIBRARY_INCLUDE)
endif()

# service
option(BUILD_SERVICE "Build ${VE_NAME} services" ON)
if (BUILD_SERVICE)
    ve_add_library(service VE_SERVICE_LIBRARY VE_SERVICE_LIBRARY_INCLUDE)
endif()

# main
#option(BUILD_MAIN "Build ${VE_NAME} Qt based Main program" ON)
#if (BUILD_MAIN)
#    add_subdirectory(main)
#endif()

message(STATUS "${VE_NAME} CMake End    ------------------------")
