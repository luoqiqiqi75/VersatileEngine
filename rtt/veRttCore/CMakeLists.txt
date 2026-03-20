# =============================================================================
# veRttCore — adds core sources to libvertt
# =============================================================================

ve_collect(${CMAKE_CURRENT_SOURCE_DIR} _RTTCORE)

target_sources(${VE_RTT_LIBRARY} PRIVATE ${_RTTCORE_ALL})

target_include_directories(${VE_RTT_LIBRARY} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
