# =============================================================================
# rtt/CMakeLists.txt - VE + RTT
# Builds libvertt from veRttCore + XService
# Self-checks: probes for Orocos RTT, returns early if not found.
# =============================================================================

# --- Required: Orocos RTT ---
find_package(OROCOS-RTT QUIET)
if(NOT OROCOS-RTT_FOUND)
    message(STATUS "")
    message(STATUS "[SKIP] libvertt — Orocos RTT not found")
    message(STATUS "       Install Orocos RTT to enable.")
    message(STATUS "")
    return()
endif()

message(STATUS "")
message(STATUS "[libvertt]  pure C++ (no Qt)")
message(STATUS "  dep: Orocos RTT ${OROCOS-RTT_VERSION} [FOUND]")

# --- Optional: evpp ---
find_package(evpp QUIET)
if(evpp_FOUND)
    message(STATUS "  dep: evpp       [FOUND]")
    set(_VE_RTT_HAS_EVPP TRUE)
else()
    message(STATUS "  dep: evpp       [NOT FOUND] — evpp backends disabled")
    set(_VE_RTT_HAS_EVPP FALSE)
endif()

# --- Output library name ---
ve_resolve_library_target_name(VE_RTT_LIBRARY vertt)

# --- Create the single library target ---
add_library(${VE_RTT_LIBRARY} STATIC)

target_compile_features(${VE_RTT_LIBRARY} PUBLIC cxx_std_17)

# Link core VE target — provides ve/global.h etc. transitively
target_link_libraries(${VE_RTT_LIBRARY} PUBLIC VersatileEngine::ve)

# nlohmann/json — used by JsonRef / Result / XService
ve_find_package(nlohmann_json
    GIT_REPO  https://github.com/nlohmann/json.git
    GIT_TAG   v3.11.3
    OPTIONS
        JSON_BuildTests OFF
        JSON_Install    OFF
)
target_link_libraries(${VE_RTT_LIBRARY} PUBLIC nlohmann_json::nlohmann_json)

# --- Conditional compile definitions ---
target_compile_definitions(${VE_RTT_LIBRARY} PUBLIC IMOL_HAS_OROCOS_RTT)
target_link_libraries(${VE_RTT_LIBRARY} PUBLIC ${OROCOS-RTT_LIBRARIES})
target_include_directories(${VE_RTT_LIBRARY} PUBLIC ${OROCOS-RTT_INCLUDE_DIRS})

if(_VE_RTT_HAS_EVPP)
    target_compile_definitions(${VE_RTT_LIBRARY} PUBLIC IMOL_HAS_EVPP)
endif()

# --- Sub-modules (add sources to ${VE_RTT_LIBRARY}) ---
add_subdirectory(veRttCore)
message(STATUS "  + veRttCore")

add_subdirectory(XService)
message(STATUS "  + XService")

message(STATUS "")
