# =============================================================================
# cpp/ros/veFastDDS/CMakeLists.txt — adds veFastDDS sources to libveros
# Origin: hemera_service/service/fastdds (2023)
# =============================================================================

ve_collect(${CMAKE_CURRENT_SOURCE_DIR} _FASTDDS)

target_sources(${VE_ROS_LIBRARY} PRIVATE ${_FASTDDS_ALL})

target_include_directories(${VE_ROS_LIBRARY} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
