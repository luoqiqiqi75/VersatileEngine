set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Output directories ---
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib" CACHE PATH "Archive output dir.")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib" CACHE PATH "Library output dir.")
set(CMAKE_PDB_OUTPUT_DIRECTORY     "${CMAKE_BINARY_DIR}/bin" CACHE PATH "PDB (MSVC debug symbol) output dir.")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" CACHE PATH "Executable/dll output dir.")

# Disallow in-source build (configure in source dir)
get_filename_component(_srcdir "${CMAKE_SOURCE_DIR}" REALPATH)
get_filename_component(_bindir "${CMAKE_BINARY_DIR}" REALPATH)
if("${_srcdir}" STREQUAL "${_bindir}")
    message(FATAL_ERROR
        "VersatileEngine: do not configure in the source directory.\n"
        "Use a separate build dir, e.g.:  cmake -B build  &&  cmake --build build"
    )
endif()

# Disallow install prefix equal to build tree
get_filename_component(_prefix_real "${CMAKE_INSTALL_PREFIX}" REALPATH)
if("${_prefix_real}" STREQUAL "${_bindir}" OR "${CMAKE_INSTALL_PREFIX}" STREQUAL "${CMAKE_BINARY_DIR}")
    message(FATAL_ERROR
        "CMAKE_INSTALL_PREFIX must not be the build tree.\n"
        "  CMAKE_INSTALL_PREFIX = ${CMAKE_INSTALL_PREFIX}\n"
        "  CMAKE_BINARY_DIR     = ${CMAKE_BINARY_DIR}"
    )
endif()

if (MSVC)
    # 启用 UTF-8 编码
    add_compile_options(/utf-8)
    
    # 注意：使用 Ninja 时，不需要 /MP，因为 Ninja 自己会并行
    # 只在使用 MSBuild 或 NMake 时才需要 /MP
    if(NOT CMAKE_GENERATOR MATCHES "Ninja")
        # 启用多处理器编译（并行编译）
        add_compile_options(/MP)
    endif()
    
    # 允许多个 MSBuild 进程同时写入同一个 PDB 文件（并行编译必需）
    add_compile_options(/FS)
    
    # asio2 headers produce many sections; /bigobj raises the limit
    add_compile_options(/bigobj)
    
    option(VE_FORCE_DEBUG_INFO "Generate pdb for crash tracking" OFF)
    if (VE_FORCE_DEBUG_INFO)
        set(CMAKE_CXX_FLAGS_RELEASE "/MD /Od /Ob2 /DNDEBUG /Zi")
        set(CMAKE_C_FLAGS_RELEASE "/MD /O2 /Ob2 /DNDEBUG /Zi")
        set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "/DEBUG /INCREMENTAL:NO")
        set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/DEBUG /INCREMENTAL:NO")
    endif ()
else ()
    # 强制调试信息（显示文件名和行号）
    # 即使在Release模式下也保留调试信息，方便问题诊断
    # 如果要最小化Release体积，在_local.cmake中设置 VE_FORCE_DEBUG_INFO=OFF
    
    # 使用 option，但允许_custom.cmake中的set()覆盖
    # 注意：如果变量已经定义（通过set），option不会覆盖
    if(NOT DEFINED VE_FORCE_DEBUG_INFO)
        option(VE_FORCE_DEBUG_INFO "Generate crash tracking" OFF)
    endif()
    
    if (VE_FORCE_DEBUG_INFO)
        if (NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
            # 在非Debug模式下添加 -g
            add_compile_options(-g)
            message(STATUS "Force debug info enabled: added -g flag for stack trace source location")
        endif()
    else()
        message(STATUS "Debug info disabled (set VE_FORCE_DEBUG_INFO=ON in cmake/_local.cmake to enable)")
    endif()
endif ()

# --- Build options ---
if(NOT DEFINED BUILD_SHARED_LIBS)
    if(IOS)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries")
    else()
        set(BUILD_SHARED_LIBS ON CACHE BOOL "Build shared libraries")
    endif()
endif()

set(CMAKE_INCLUDE_CURRENT_DIR ON)