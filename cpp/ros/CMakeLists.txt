# =============================================================================
# cpp/ros/CMakeLists.txt
# ROS module hub — builds a single libveros from all enabled sub-modules
#
# Self-checks: probes for catkin (ROS 1) or ament_cmake (ROS 2).
# Returns early if neither is found.
# =============================================================================

# --- Probe for ROS ---
find_package(catkin QUIET)
find_package(ament_cmake QUIET)

if(catkin_FOUND)
    set(VE_ROS_FLAVOUR "ROS1")
    message(STATUS "")
    message(STATUS "[libveros]  catkin (ROS 1)")
elseif(ament_cmake_FOUND)
    set(VE_ROS_FLAVOUR "ROS2")
    message(STATUS "")
    message(STATUS "[libveros]  ament_cmake (ROS 2)")
else()
    message(STATUS "")
    message(STATUS "[SKIP] libveros — neither catkin (ROS 1) nor ament_cmake (ROS 2) found")
    message(STATUS "       Source your ROS setup.bash before running cmake to enable.")
    message(STATUS "")
    return()
endif()

# --- Sub-module options ---
option(VE_BUILD_ROS_CORE    "Build veRosBase (hemera compat, engine, math, data_service)"  ON)
option(VE_BUILD_FASTDDS     "Build veFastDDS (Ros2FastDDSServer)"                          OFF)

# --- Output library name ---
if(WIN32)
    set(VE_ROS_LIBRARY libveros)
else()
    set(VE_ROS_LIBRARY veros)
endif()

# ==========================================================
# Create the single shared library target
# ==========================================================
add_library(${VE_ROS_LIBRARY} SHARED)
target_compile_features(${VE_ROS_LIBRARY} PUBLIC cxx_std_17)
target_compile_definitions(${VE_ROS_LIBRARY} PRIVATE VE_LIBRARY)

# Link libve core — provides ve/global.h, ve/core/base.h, ve/core/data.h, etc.
if(WIN32)
    target_link_libraries(${VE_ROS_LIBRARY} PUBLIC libve)
else()
    target_link_libraries(${VE_ROS_LIBRARY} PUBLIC ve)
endif()

# --- Deps: Eigen3 (for math_service / eigen_yaml_convert) ---
find_package(Eigen3 QUIET)
if(Eigen3_FOUND)
    target_link_libraries(${VE_ROS_LIBRARY} PUBLIC Eigen3::Eigen)
    message(STATUS "  dep: Eigen3   [FOUND]")
else()
    message(WARNING "  dep: Eigen3   [NOT FOUND] — math features will not compile")
endif()

# --- Platform libs ---
if(UNIX AND NOT APPLE)
    target_link_libraries(${VE_ROS_LIBRARY} PRIVATE dl)
endif()

# ==========================================================
# Sub-modules
# ==========================================================
if(VE_BUILD_ROS_CORE)
    add_subdirectory(veRosBase)
    message(STATUS "  + veRosBase (common, engine, math, data_service)")
endif()

if(VE_BUILD_FASTDDS AND VE_BUILD_ROS_CORE)
    find_package(fastrtps QUIET)
    find_package(fastcdr QUIET)
    if(fastrtps_FOUND AND fastcdr_FOUND)
        target_link_libraries(${VE_ROS_LIBRARY} PUBLIC fastrtps fastcdr)
        add_subdirectory(veFastDDS)
        message(STATUS "  + veFastDDS")
    else()
        message(STATUS "  - veFastDDS  (fastrtps/fastcdr not found)")
    endif()
endif()

message(STATUS "")
