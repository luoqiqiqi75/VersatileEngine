# =============================================================================
# cpp/ros/veFastDDS/CMakeLists.txt — adds veFastDDS sources to libvedds
# =============================================================================

ve_collect(${CMAKE_CURRENT_SOURCE_DIR} _FASTDDS)

target_sources(${VE_DDS_LIBRARY} PRIVATE ${_FASTDDS_ALL})

target_include_directories(${VE_DDS_LIBRARY} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
