# =============================================================================
# ve_deps.cmake — VersatileEngine 依赖管理
# =============================================================================
#
# 内置依赖 (随 deps/ 目录分发):
#   ve_dep_spdlog  — spdlog 1.12 (logging, header-only)
#   ve_dep_asio    — standalone asio 1.29 (header-only)
#   ve_dep_asio2   — asio2 2.9 (high-level networking, header-only)
#
# 外部依赖由各子项目按需调用 ve_find_package() 声明:
#   ve/            -> yaml-cpp::yaml-cpp, simdjson::simdjson
#   rtt/           -> nlohmann_json::nlohmann_json
#
# ve_find_package 查找顺序: 本地路径 → find_package → FetchContent
# 本地路径在 cmake/_local.cmake 中配置 (gitignored)
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

# =============================================================================
# ve_find_package 宏 (供各子项目按需调用)
# =============================================================================

include(ve_find_package)

# =============================================================================
# Summary
# =============================================================================

message(STATUS "")
message(STATUS "Bundled dependencies:")
message(STATUS "  spdlog   (${VE_DEPS_3RD}/spdlog)")
message(STATUS "  asio     (${VE_DEPS_3RD}/asio)")
message(STATUS "  asio2    (${VE_DEPS_ASIO2_ROOT}/include/asio2)")
