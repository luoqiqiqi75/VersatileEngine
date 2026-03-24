# =============================================================================
# ros/CMakeLists.txt - VE + ROS/DDS (FastDDS) -> libveros
# Builds libveros (or vedds): ve::dds and ve::ros integration for Node <-> DDS
# Unified include/ + src/ structure similar to qt/. No catkin/ament.
# =============================================================================

find_package(fastrtps QUIET)
find_package(fastcdr QUIET)

if(NOT fastrtps_FOUND OR NOT fastcdr_FOUND)
    message(STATUS "")
    message(STATUS "[SKIP] libveros — fastrtps or fastcdr not found")
    message(STATUS "       Install eProsima Fast DDS to enable.")
    message(STATUS "")
    return()
endif()

message(STATUS "")
message(STATUS "[libveros]  FastDDS (fastrtps ${fastrtps_VERSION}, fastcdr ${fastcdr_VERSION})")

# --- Output library name ---
if(WIN32)
    set(VE_ROS_LIBRARY libveros)
else()
    set(VE_ROS_LIBRARY veros)
endif()
set(VE_ROS_LIBRARY ${VE_ROS_LIBRARY} PARENT_SCOPE)

# --- Create shared library ---
add_library(${VE_ROS_LIBRARY} SHARED)

target_compile_features(${VE_ROS_LIBRARY} PUBLIC cxx_std_17)
target_compile_definitions(${VE_ROS_LIBRARY} PRIVATE VE_LIBRARY)

# --- Link dependencies ---
if(WIN32)
    target_link_libraries(${VE_ROS_LIBRARY} PUBLIC libve)
else()
    target_link_libraries(${VE_ROS_LIBRARY} PUBLIC ve)
endif()

target_link_libraries(${VE_ROS_LIBRARY} PUBLIC fastrtps fastcdr)

# yaml-cpp for CommandService YAML serialization
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

# ==========================================================
# Public headers — single unified include/
# ==========================================================
target_include_directories(${VE_ROS_LIBRARY} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ==========================================================
# Library sources using ve_collect (now works with new structure)
# ==========================================================
ve_collect(${CMAKE_CURRENT_SOURCE_DIR} _VEROS)

target_sources(${VE_ROS_LIBRARY} PRIVATE ${_VEROS_ALL})

# PRIVATE include for src/ headers (if any internal .h)
target_include_directories(${VE_ROS_LIBRARY} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

message(STATUS "  + ve::dds (Participant, DynamicTypes, Bridge, Topic, Service)")
message(STATUS "  + ve::ros (CommandService, RosModule, data helpers)")
message(STATUS "  + Unified include/ve/ros/ + src/{dds,service,module,data}/")
message(STATUS "")

# Optional program/ for example modules or bridge DLLs
# add_subdirectory(program) if VE_BUILD_ROS_PROGRAM or similar
