# =============================================================================
# ve_3rdparty.cmake - Third-party dependency targets for VersatileEngine
# =============================================================================
#
# All 3rdparty deps are bundled under 3rdparty/asio2/ (header-only).
# This file creates clean INTERFACE library targets for each.
#
# Targets:
#   ve_3rdparty_spdlog  - spdlog 1.12 (logging)
#   ve_3rdparty_asio    - standalone asio 1.29 (event loop, thread pool, timers)
#   ve_3rdparty_asio2   - asio2 2.9 (high-level networking + all deps)
# =============================================================================

set(VE_3RDPARTY_ROOT "${CMAKE_SOURCE_DIR}/3rdparty/asio2")
set(VE_3RDPARTY_3RD  "${VE_3RDPARTY_ROOT}/3rd")

# --- spdlog (header-only) ---
# Usage: target_link_libraries(xxx PRIVATE ve_3rdparty_spdlog)
# Provides: spdlog/spdlog.h, spdlog/sinks/*, spdlog/async.h, etc.
add_library(ve_3rdparty_spdlog INTERFACE)
target_include_directories(ve_3rdparty_spdlog INTERFACE "${VE_3RDPARTY_3RD}")

# --- standalone asio (header-only) ---
# Usage: target_link_libraries(xxx PRIVATE ve_3rdparty_asio)
# Provides: asio/io_context.hpp, asio/thread_pool.hpp, asio/post.hpp, etc.
# Note: asio requires platform networking libs on Windows
add_library(ve_3rdparty_asio INTERFACE)
target_include_directories(ve_3rdparty_asio INTERFACE "${VE_3RDPARTY_3RD}")
target_compile_definitions(ve_3rdparty_asio INTERFACE
    ASIO_STANDALONE
    ASIO_HEADER_ONLY
)
if(WIN32)
    target_link_libraries(ve_3rdparty_asio INTERFACE ws2_32 mswsock)
elseif(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(ve_3rdparty_asio INTERFACE Threads::Threads)
endif()

# --- asio2 (header-only, includes asio + spdlog + cereal + fmt) ---
# Usage: target_link_libraries(xxx PRIVATE ve_3rdparty_asio2)
# Provides: everything in asio2/*, asio/*, spdlog/*, cereal/*, fmt/*
add_library(ve_3rdparty_asio2 INTERFACE)
target_include_directories(ve_3rdparty_asio2 INTERFACE
    "${VE_3RDPARTY_ROOT}/include"
    "${VE_3RDPARTY_3RD}"
)
target_link_libraries(ve_3rdparty_asio2 INTERFACE ve_3rdparty_asio)

message(STATUS "3rdparty: spdlog (${VE_3RDPARTY_3RD}/spdlog)")
message(STATUS "3rdparty: asio   (${VE_3RDPARTY_3RD}/asio)")
message(STATUS "3rdparty: asio2  (${VE_3RDPARTY_ROOT}/include/asio2)")