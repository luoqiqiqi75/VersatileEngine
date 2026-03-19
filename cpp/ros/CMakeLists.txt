# =============================================================================
# cpp/ros/CMakeLists.txt — DDS adapter (FastDDS)
# Builds libvedds: ve::dds namespace, Node-DDS bridge, Dynamic Types
# No catkin/ament required — only FastDDS (fastrtps + fastcdr)
# =============================================================================

find_package(fastrtps QUIET)
find_package(fastcdr  QUIET)

if(NOT fastrtps_FOUND OR NOT fastcdr_FOUND)
    message(STATUS "")
    message(STATUS "[SKIP] libvedds — fastrtps or fastcdr not found")
    message(STATUS "       Install eProsima Fast DDS to enable.")
    message(STATUS "")
    return()
endif()

message(STATUS "")
message(STATUS "[libvedds]  FastDDS (fastrtps ${fastrtps_VERSION}, fastcdr ${fastcdr_VERSION})")

# --- Output library name ---
if(WIN32)
    set(VE_DDS_LIBRARY libvedds)
else()
    set(VE_DDS_LIBRARY vedds)
endif()

# --- Create shared library ---
add_library(${VE_DDS_LIBRARY} SHARED)
target_compile_features(${VE_DDS_LIBRARY} PUBLIC cxx_std_17)
target_compile_definitions(${VE_DDS_LIBRARY} PRIVATE VE_LIBRARY)

# --- Link dependencies ---
if(WIN32)
    target_link_libraries(${VE_DDS_LIBRARY} PUBLIC libve)
else()
    target_link_libraries(${VE_DDS_LIBRARY} PUBLIC ve)
endif()

target_link_libraries(${VE_DDS_LIBRARY} PUBLIC fastrtps fastcdr)

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
target_link_libraries(${VE_DDS_LIBRARY} PUBLIC yaml-cpp::yaml-cpp)

# --- Sub-modules ---
add_subdirectory(veFastDDS)

message(STATUS "  + veFastDDS (Participant, Topic, Service, DynTypes, Bridge)")
message(STATUS "")
