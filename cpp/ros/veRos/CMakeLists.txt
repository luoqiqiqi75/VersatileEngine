# =============================================================================
# cpp/ros/veRos/CMakeLists.txt — adds veRos sources to libvedds
# Provides: CommandService, ROS module (DDS environment scan & service startup)
# =============================================================================

ve_collect(${CMAKE_CURRENT_SOURCE_DIR} _VEROS)

target_sources(${VE_DDS_LIBRARY} PRIVATE ${_VEROS_ALL})

target_include_directories(${VE_DDS_LIBRARY} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
