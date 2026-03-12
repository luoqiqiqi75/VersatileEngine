# =============================================================================
# cpp/rtt/CMakeLists.txt
# RTT module hub — builds a single libvertt from veRttCore + XService
# =============================================================================

message(STATUS "")
message(STATUS "[libvertt]  pure C++ (no Qt)")

# --- Optional: Orocos RTT ---
find_package(OROCOS-RTT QUIET)
if(OROCOS-RTT_FOUND)
    message(STATUS "  dep: Orocos RTT ${OROCOS-RTT_VERSION} [FOUND]")
    set(_VE_RTT_HAS_OROCOS TRUE)
else()
    message(STATUS "  dep: Orocos RTT [NOT FOUND] — rtt_loop backend disabled")
    set(_VE_RTT_HAS_OROCOS FALSE)
endif()

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
if(WIN32)
    set(VE_RTT_LIBRARY libvertt)
else()
    set(VE_RTT_LIBRARY vertt)
endif()

# --- Create the single library target ---
add_library(${VE_RTT_LIBRARY} STATIC)

target_compile_features(${VE_RTT_LIBRARY} PUBLIC cxx_std_17)

# Link libve — provides ve/global.h etc. transitively
if(WIN32)
    target_link_libraries(${VE_RTT_LIBRARY} PUBLIC libve)
else()
    target_link_libraries(${VE_RTT_LIBRARY} PUBLIC ve)
endif()

# nlohmann/json — used by JsonRef / Result / XService
target_link_libraries(${VE_RTT_LIBRARY} PUBLIC nlohmann_json::nlohmann_json)

# --- Conditional compile definitions ---
if(_VE_RTT_HAS_OROCOS)
    target_compile_definitions(${VE_RTT_LIBRARY} PUBLIC IMOL_HAS_OROCOS_RTT)
    target_link_libraries(${VE_RTT_LIBRARY} PUBLIC ${OROCOS-RTT_LIBRARIES})
    target_include_directories(${VE_RTT_LIBRARY} PUBLIC ${OROCOS-RTT_INCLUDE_DIRS})
endif()
if(_VE_RTT_HAS_EVPP)
    target_compile_definitions(${VE_RTT_LIBRARY} PUBLIC IMOL_HAS_EVPP)
endif()

# --- Sub-modules (add sources to ${VE_RTT_LIBRARY}) ---
add_subdirectory(veRttCore)
message(STATUS "  + veRttCore")

add_subdirectory(XService)
message(STATUS "  + XService")

message(STATUS "")
