# =============================================================================
# ve_deps.cmake — VersatileEngine 依赖管理
# =============================================================================
#
# 内置依赖 (随 deps/ 目录分发):
#   ve_dep_spdlog  — spdlog 1.12 (logging, header-only)
#   ve_dep_asio    — standalone asio 1.29 (header-only)
#   ve_dep_asio2   — asio2 2.9 (high-level networking, header-only)
#
# 外部依赖 (通过 ve_find_package 统一管理, 消费方直接用原始 target):
#   yaml-cpp::yaml-cpp       — yaml-cpp (YAML parsing)
#   pugixml::pugixml         — pugixml (XML parsing)
#   nlohmann_json::nlohmann_json — nlohmann/json (JSON parsing, header-only)
#   simdjson::simdjson       — simdjson (high-perf JSON parsing)
#
# 外部依赖查找顺序: 本地路径 → find_package → FetchContent
# 本地路径在 cmake/_local.cmake 中配置 (gitignored)
# =============================================================================

set(VE_DEPS_ROOT "${CMAKE_SOURCE_DIR}/deps")
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
# 外部依赖 (via ve_find_package)
# =============================================================================

include(ve_find_package)

message(STATUS "")
message(STATUS "External dependencies:")

# --- yaml-cpp ---
ve_find_package(yaml-cpp
    GIT_REPO  https://github.com/jbeder/yaml-cpp.git
    GIT_TAG   0.8.0
    OPTIONS
        YAML_CPP_BUILD_TOOLS   OFF
        YAML_CPP_BUILD_CONTRIB ON
        YAML_BUILD_SHARED_LIBS OFF
        YAML_CPP_INSTALL       OFF
)

# --- pugixml ---
ve_find_package(pugixml
    GIT_REPO  https://github.com/zeux/pugixml.git
    GIT_TAG   v1.14
    OPTIONS
        PUGIXML_INSTALL OFF
)

# --- nlohmann/json ---
ve_find_package(nlohmann_json
    GIT_REPO  https://github.com/nlohmann/json.git
    GIT_TAG   v3.11.3
    OPTIONS
        JSON_BuildTests OFF
        JSON_Install    OFF
)

# --- simdjson ---
ve_find_package(simdjson
    GIT_REPO  https://github.com/simdjson/simdjson.git
    GIT_TAG   v3.10.1
    OPTIONS
        SIMDJSON_DEVELOPER_MODE OFF
)

# =============================================================================
# Summary
# =============================================================================

message(STATUS "")
message(STATUS "Bundled dependencies:")
message(STATUS "  spdlog   (${VE_DEPS_3RD}/spdlog)")
message(STATUS "  asio     (${VE_DEPS_3RD}/asio)")
message(STATUS "  asio2    (${VE_DEPS_ASIO2_ROOT}/include/asio2)")
