# =============================================================================
# ve_deps.cmake - Bundled dependency targets for VersatileEngine
# =============================================================================
#
# Targets:
#   ve_dep_spdlog  - spdlog 1.12 (logging)
#   ve_dep_asio    - standalone asio 1.29 (event loop, thread pool, timers)
#   ve_dep_asio2   - asio2 2.9 (high-level networking + all deps)
#   ve_dep_yaml    - yaml-cpp 0.9 (bundled source OR system library)
#   ve_dep_pugixml - pugixml 1.15 (XML parsing, bundled source OR system library)
#   ve_dep_json    - nlohmann/json (JSON parsing, header-only)
# =============================================================================

set(VE_DEPS_ROOT "${CMAKE_SOURCE_DIR}/deps")
set(VE_DEPS_ASIO2_ROOT "${VE_DEPS_ROOT}/asio2")
set(VE_DEPS_3RD  "${VE_DEPS_ASIO2_ROOT}/3rd")

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

# --- yaml-cpp (bundled source OR system library) ---
# VE_USE_SYSTEM_YAML=ON  → find_package(yaml-cpp), link system lib
# VE_USE_SYSTEM_YAML=OFF → compile deps/yaml-cpp/ as static lib via add_subdirectory
#
# Either way, consumers just:
#   target_link_libraries(xxx PUBLIC ve_dep_yaml)
#   #include "yaml-cpp/yaml.h"
option(VE_USE_SYSTEM_YAML "Use system-installed yaml-cpp instead of bundled source" OFF)

add_library(ve_dep_yaml INTERFACE)

if(VE_USE_SYSTEM_YAML)
    find_package(yaml-cpp REQUIRED)
    target_link_libraries(ve_dep_yaml INTERFACE yaml-cpp::yaml-cpp)
    message(STATUS "deps: yaml-cpp [SYSTEM]  ${yaml-cpp_VERSION}")
else()
    # Build as static lib, disable tests/tools/install
    set(YAML_CPP_BUILD_TOOLS   OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_CONTRIB ON  CACHE BOOL "" FORCE)
    set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_INSTALL       OFF CACHE BOOL "" FORCE)
    add_subdirectory("${VE_DEPS_ROOT}/yaml-cpp" "${CMAKE_BINARY_DIR}/_deps/yaml-cpp")
    # yaml-cpp's CMakeLists automatically defines YAML_CPP_STATIC_DEFINE
    # when YAML_BUILD_SHARED_LIBS=OFF, so YAML_CPP_API is empty. Perfect.
    target_link_libraries(ve_dep_yaml INTERFACE yaml-cpp::yaml-cpp)
    message(STATUS "deps: yaml-cpp [BUNDLED] 0.9 (static)")
endif()

# --- pugixml (bundled source OR system library) ---
# VE_USE_SYSTEM_PUGIXML=ON  → find_package(pugixml), link system lib
# VE_USE_SYSTEM_PUGIXML=OFF → compile deps/pugixml/ as static lib via add_subdirectory
#
# Consumers:
#   target_link_libraries(xxx PUBLIC ve_dep_pugixml)
#   #include "pugixml.hpp"
option(VE_USE_SYSTEM_PUGIXML "Use system-installed pugixml instead of bundled source" OFF)

add_library(ve_dep_pugixml INTERFACE)

if(VE_USE_SYSTEM_PUGIXML)
    find_package(pugixml REQUIRED)
    target_link_libraries(ve_dep_pugixml INTERFACE pugixml::pugixml)
    message(STATUS "deps: pugixml  [SYSTEM]  ${pugixml_VERSION}")
else()
    set(PUGIXML_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory("${VE_DEPS_ROOT}/pugixml" "${CMAKE_BINARY_DIR}/_deps/pugixml")
    target_link_libraries(ve_dep_pugixml INTERFACE pugixml::pugixml)
    message(STATUS "deps: pugixml  [BUNDLED] 1.15 (static)")
endif()

# --- nlohmann/json (header-only) ---
add_library(ve_dep_json INTERFACE)
target_include_directories(ve_dep_json INTERFACE "${VE_DEPS_ROOT}")

message(STATUS "")
message(STATUS "deps: spdlog   (${VE_DEPS_3RD}/spdlog)")
message(STATUS "deps: asio     (${VE_DEPS_3RD}/asio)")
message(STATUS "deps: asio2    (${VE_DEPS_ASIO2_ROOT}/include/asio2)")
message(STATUS "deps: json     (${VE_DEPS_ROOT}/nlohmann)")