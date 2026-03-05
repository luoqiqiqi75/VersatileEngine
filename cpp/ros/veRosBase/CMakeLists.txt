# =============================================================================
# cpp/ros/veRosBase/CMakeLists.txt — adds veRosBase sources to libveros
# Provides: ve/ros/core/common.h (hemera compat bridge),
#           ve/ros/core/math_service.h, ve/ros/service/data_service.h,
#           hemera::engine, hemera::util
# =============================================================================

ve_collect(${CMAKE_CURRENT_SOURCE_DIR} _ROS_BASE)

target_sources(${VE_ROS_LIBRARY} PRIVATE ${_ROS_BASE_ALL})

target_include_directories(${VE_ROS_LIBRARY} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
