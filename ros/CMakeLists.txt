# =============================================================================
# ros/CMakeLists.txt
# Unified ve/ros module with backend-neutral public headers and pluggable
# runtime implementations. Mainline backend is rclcpp; Fast DDS is optional.
# =============================================================================

option(VE_ROS_WITH_RCLCPP "Build ve/ros with rclcpp backend support" ON)
option(VE_ROS_WITH_FASTDDS "Build ve/ros with Fast DDS backend support" ON)

set(_VE_ROS_HAS_RCLCPP OFF)
set(_VE_ROS_HAS_FASTDDS OFF)

if(VE_ROS_WITH_RCLCPP)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    find_package(ament_cmake QUIET)
    if(Python3_FOUND AND ament_cmake_FOUND)
        find_package(rclcpp QUIET)
        if(rclcpp_FOUND)
            set(_VE_ROS_HAS_RCLCPP ON)
        endif()
    endif()
endif()

if(VE_ROS_WITH_FASTDDS)
    find_package(fastrtps QUIET)
    find_package(fastcdr QUIET)
    if(fastrtps_FOUND AND fastcdr_FOUND)
        # Check FastDDS version - need 2.3+ for delete_contained_entities
        if(fastrtps_VERSION VERSION_LESS "2.3.0")
            message(STATUS "  - FastDDS ${fastrtps_VERSION} < 2.3.0: too old, skipping")
        else()
            set(_VE_ROS_HAS_FASTDDS ON)
        endif()
    endif()
endif()

if(NOT _VE_ROS_HAS_RCLCPP AND NOT _VE_ROS_HAS_FASTDDS)
    message(STATUS "")
    message(STATUS "[SKIP] libveros — neither rclcpp nor Fast DDS backends are available")
    message(STATUS "       Source a ROS2 environment for rclcpp or install Fast DDS.")
    message(STATUS "")
    return()
endif()

message(STATUS "")
message(STATUS "[libveros]  unified ve/ros")
if(_VE_ROS_HAS_RCLCPP)
    message(STATUS "  + backend: rclcpp")
endif()
if(_VE_ROS_HAS_FASTDDS)
    message(STATUS "  + backend: Fast DDS (${fastrtps_VERSION}, ${fastcdr_VERSION})")
endif()

ve_resolve_library_target_name(VE_ROS_LIBRARY veros)
set(VE_ROS_LIBRARY ${VE_ROS_LIBRARY} PARENT_SCOPE)

add_library(${VE_ROS_LIBRARY} SHARED)
target_compile_features(${VE_ROS_LIBRARY} PUBLIC cxx_std_17)
target_compile_definitions(${VE_ROS_LIBRARY} PRIVATE VE_LIBRARY)

target_link_libraries(${VE_ROS_LIBRARY} PUBLIC VersatileEngine::ve)

ve_find_package(yaml-cpp
    GIT_REPO  https://github.com/jbeder/yaml-cpp.git
    GIT_TAG   0.8.0
    OPTIONS
        YAML_CPP_BUILD_TOOLS   OFF
        YAML_CPP_BUILD_CONTRIB ON
        YAML_BUILD_SHARED_LIBS OFF
        YAML_CPP_INSTALL       OFF
)
target_link_libraries(${VE_ROS_LIBRARY} PUBLIC yaml-cpp::yaml-cpp)

target_include_directories(${VE_ROS_LIBRARY} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(${VE_ROS_LIBRARY} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

file(GLOB_RECURSE _ROS_PUBLIC_HEADERS include/ve/ros/*.h)
file(GLOB _ROS_UMBRELLA_HEADERS include/veRos)
file(GLOB_RECURSE _ROS_BASE_SRC src/base/*.cpp src/base/*.h)
file(GLOB_RECURSE _ROS_MODULE_SRC src/module/*.cpp src/module/*.h)
file(GLOB_RECURSE _ROS_SERVICE_SRC src/service/*.cpp src/service/*.h)

target_sources(${VE_ROS_LIBRARY} PRIVATE
    ${_ROS_PUBLIC_HEADERS}
    ${_ROS_UMBRELLA_HEADERS}
    ${_ROS_BASE_SRC}
    ${_ROS_MODULE_SRC}
    ${_ROS_SERVICE_SRC}
)

if(_VE_ROS_HAS_RCLCPP)
    file(GLOB_RECURSE _ROS_RCLCPP_SRC src/backend/rclcpp/*.cpp src/backend/rclcpp/*.h)

    # Check if rclcpp has typesupport_helpers (Galactic+)
    # Foxy (rclcpp 2.x) doesn't have this, so exclude dynamic_typesupport_bridge
    if(rclcpp_VERSION VERSION_LESS "3.0.0")
        list(FILTER _ROS_RCLCPP_SRC EXCLUDE REGEX "dynamic_typesupport_bridge\\.(cpp|h)$")
        message(STATUS "  - rclcpp ${rclcpp_VERSION} < 3.0 (Foxy): excluding dynamic typesupport bridge")
        message(STATUS "  - rclcpp ${rclcpp_VERSION} < 3.0 (Foxy): GenericSubscription/Publisher not available")
    else()
        target_compile_definitions(${VE_ROS_LIBRARY} PRIVATE VE_ROS_HAS_DYNAMIC_TYPESUPPORT=1)
        target_compile_definitions(${VE_ROS_LIBRARY} PRIVATE VE_ROS_HAS_GENERIC_PUBSUB=1)
    endif()

    target_sources(${VE_ROS_LIBRARY} PRIVATE ${_ROS_RCLCPP_SRC})
    ament_target_dependencies(${VE_ROS_LIBRARY} PUBLIC rclcpp)
    target_compile_definitions(${VE_ROS_LIBRARY} PRIVATE VE_ROS_HAS_RCLCPP=1)
endif()

if(_VE_ROS_HAS_FASTDDS)
    file(GLOB_RECURSE _ROS_FASTDDS_SRC src/backend/fastdds/*.cpp src/backend/fastdds/*.h)
    target_sources(${VE_ROS_LIBRARY} PRIVATE ${_ROS_FASTDDS_SRC})
    target_link_libraries(${VE_ROS_LIBRARY} PUBLIC fastrtps fastcdr)
    target_compile_definitions(${VE_ROS_LIBRARY} PRIVATE VE_ROS_HAS_FASTDDS=1)
endif()

message(STATUS "  + public headers: include/ve/ros/*.h")
message(STATUS "  + source tree: src/base, src/backend/*, src/module, src/service")
message(STATUS "")
