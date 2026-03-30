# =============================================================================
# ve_deps.cmake — VersatileEngine 依赖管理
# =============================================================================
#
# 内置依赖 (随 deps/ 目录分发):
#   ve_dep_spdlog    — spdlog 1.12 (logging, header-only)
#   ve_dep_asio      — standalone asio 1.29 (header-only)
#   ve_dep_asio2     — asio2 2.9 (high-level networking, header-only)
#   ve_dep_simdjson  — simdjson（deps/simdjson，add_subdirectory，始终静态归档，链接 simdjson::simdjson）
#   ve_dep_pugixml   — pugixml（deps/pugixml，静态库，链接 pugixml::pugixml）
#
# 外部依赖由各子项目按需调用 ve_find_package() 声明:
#   rtt/           -> nlohmann_json::nlohmann_json
#
# ve_find_package 查找顺序: 本地路径 → find_package → FetchContent
# 本地路径在 cmake/_local.cmake 中配置 (gitignored)；simdjson 不在此列，仅用 deps/simdjson
#
# VE_DEPS_ROOT: 若调用方已设置 (CACHE)，则沿用；否则使用 VE 自身目录下的 deps，
# 以便 add_subdirectory(VersatileEngine) 时 deps 仍指向 VE/deps。
# =============================================================================

if(NOT DEFINED VE_DEPS_ROOT)
  set(VE_DEPS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/deps")
endif()
set(VE_DEPS_ASIO2_ROOT "${VE_DEPS_ROOT}/asio2")
set(VE_DEPS_3RD  "${VE_DEPS_ASIO2_ROOT}/3rd")

# =============================================================================
# 内置依赖 (shipped in deps/)
# =============================================================================

# --- spdlog (header-only) ---
add_library(ve_dep_spdlog INTERFACE)
target_include_directories(ve_dep_spdlog INTERFACE "${VE_DEPS_3RD}")

# --- standalone asio (header-only) ---
add_library(ve_dep_asio INTERFACE)
target_include_directories(ve_dep_asio INTERFACE "${VE_DEPS_3RD}")
target_compile_definitions(ve_dep_asio INTERFACE ASIO_STANDALONE ASIO_HEADER_ONLY)
if(WIN32)
    target_link_libraries(ve_dep_asio INTERFACE ws2_32 mswsock)
elseif(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(ve_dep_asio INTERFACE Threads::Threads)
endif()

# --- asio2 (header-only, includes asio + spdlog + cereal + fmt) ---
add_library(ve_dep_asio2 INTERFACE)
target_include_directories(ve_dep_asio2 INTERFACE
    "${VE_DEPS_ASIO2_ROOT}/include"
    "${VE_DEPS_3RD}"
)
target_link_libraries(ve_dep_asio2 INTERFACE ve_dep_asio)

# --- simdjson (sources under deps/simdjson, same layout as other bundled deps) ---
# Upstream uses add_library(simdjson ...) without STATIC; type follows BUILD_SHARED_LIBS.
# Force OFF only for this subdir so libve always links a static .lib/.a, then restore.
set(VE_DEPS_SIMDJSON_ROOT "${VE_DEPS_ROOT}/simdjson")
if(NOT EXISTS "${VE_DEPS_SIMDJSON_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR
        "Bundled simdjson missing: ${VE_DEPS_SIMDJSON_ROOT}\n"
        "Place the simdjson source tree at deps/simdjson (with top-level CMakeLists.txt).")
endif()

set(_VE_DEPS_SIMDJSON_SAVE_BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF)
set(SIMDJSON_BUILD_STATIC_LIB OFF CACHE BOOL "simdjson: main target is static; skip simdjson_static" FORCE)
add_subdirectory("${VE_DEPS_SIMDJSON_ROOT}" "${CMAKE_BINARY_DIR}/_deps/simdjson")
# libsimdjson.a is linked into libve.so; without PIC, aarch64 ld fails with
# R_AARCH64_ADR_PREL_PG_HI21 ... recompile with -fPIC
if(TARGET simdjson)
  set_target_properties(simdjson PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
if(TARGET simdjson_static)
  set_target_properties(simdjson_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
set(BUILD_SHARED_LIBS "${_VE_DEPS_SIMDJSON_SAVE_BUILD_SHARED_LIBS}")
unset(_VE_DEPS_SIMDJSON_SAVE_BUILD_SHARED_LIBS)

add_library(ve_dep_simdjson INTERFACE)
target_link_libraries(ve_dep_simdjson INTERFACE simdjson::simdjson)

# --- pugixml (XML; deps/pugixml by default, static .a/.lib into libve) ---
# Override with VE_DEP_pugixml in cmake/_local.cmake (same pattern as ve_find_package).
set(VE_DEPS_PUGIXML_ROOT "${VE_DEPS_ROOT}/pugixml")
if(DEFINED VE_DEP_pugixml AND EXISTS "${VE_DEP_pugixml}/CMakeLists.txt")
    set(_VE_PUGIXML_SRC_ROOT "${VE_DEP_pugixml}")
elseif(EXISTS "${VE_DEPS_PUGIXML_ROOT}/CMakeLists.txt")
    set(_VE_PUGIXML_SRC_ROOT "${VE_DEPS_PUGIXML_ROOT}")
else()
    message(FATAL_ERROR
        "pugixml not found. Expected ${VE_DEPS_PUGIXML_ROOT} or set VE_DEP_pugixml in cmake/_local.cmake")
endif()
set(VE_DEPS_PUGIXML_ROOT "${_VE_PUGIXML_SRC_ROOT}")

set(_VE_DEPS_PUGIXML_SAVE_BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF)
set(PUGIXML_BUILD_TESTS OFF CACHE BOOL "pugixml: skip tests when built as VE subdir" FORCE)
set(PUGIXML_INSTALL OFF CACHE BOOL "pugixml: no install rules when bundled in libve" FORCE)

# Performance optimizations for pugixml:
# - Default mode (non-COMPACT) for best performance
# - Disable XPath support (not used in VE schema serialization)
# - Keep exceptions and STL for compatibility with VE
set(PUGIXML_NO_XPATH ON CACHE BOOL "pugixml: disable XPath (not used in VE)" FORCE)

add_subdirectory("${_VE_PUGIXML_SRC_ROOT}" "${CMAKE_BINARY_DIR}/_deps/pugixml")
set(BUILD_SHARED_LIBS "${_VE_DEPS_PUGIXML_SAVE_BUILD_SHARED_LIBS}")
unset(_VE_DEPS_PUGIXML_SAVE_BUILD_SHARED_LIBS)

# PIC required for linking static lib into shared libve on Linux/macOS
if(TARGET pugixml-static)
    set_target_properties(pugixml-static PROPERTIES POSITION_INDEPENDENT_CODE ON)
    # Compiler-specific optimizations (only in Release builds to avoid conflicts)
    if(MSVC)
        target_compile_options(pugixml-static PRIVATE $<$<CONFIG:Release>:/O2 /Ob2>)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pugixml-static PRIVATE $<$<CONFIG:Release>:-O3>)
        # Only use -march=native on x86/x64, not on ARM/other architectures
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i686")
            target_compile_options(pugixml-static PRIVATE $<$<CONFIG:Release>:-march=native>)
        endif()
    endif()
endif()

add_library(ve_dep_pugixml INTERFACE)
target_link_libraries(ve_dep_pugixml INTERFACE pugixml::pugixml)

# =============================================================================
# ve_find_package 宏 (供各子项目按需调用)
# =============================================================================

include(ve_find_package)

# =============================================================================
# Summary
# =============================================================================

message(STATUS "")
message(STATUS "Bundled dependencies:")
message(STATUS "  spdlog    (${VE_DEPS_3RD}/spdlog)")
message(STATUS "  asio      (${VE_DEPS_3RD}/asio)")
message(STATUS "  asio2     (${VE_DEPS_ASIO2_ROOT}/include/asio2)")
message(STATUS "  simdjson  (${VE_DEPS_SIMDJSON_ROOT})")
message(STATUS "  pugixml   (${VE_DEPS_PUGIXML_ROOT})")
