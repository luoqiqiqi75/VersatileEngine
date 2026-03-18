# =============================================================================
# core/CMakeLists.txt — pure C++ base library -> libve
#
# Provides: ve/global.h (VE_API), ve/core/base.h (Object, Manager),
#           ve/core/data.h (AnyData, YAML), ve/core/log.h,
#           ve/core/factory.h, platform/ (rescue, StackWalker)
# =============================================================================

message(STATUS "")
message(STATUS "[libve]  pure C++ core")

ve_collect(${CMAKE_CURRENT_SOURCE_DIR} _CORE)

# --- Platform-specific sources ---
if(WIN32)
    set(_PLAT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/platform/win)
elseif(UNIX AND NOT APPLE AND NOT ANDROID)
    set(_PLAT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/platform/linux)
else()
    set(_PLAT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/platform/unsupported)
endif()

file(GLOB_RECURSE _PLAT_SOURCES ${_PLAT_DIR}/*.h ${_PLAT_DIR}/*.cpp)

# --- Library target ---
if(WIN32)
    set(VE_CORE_LIBRARY libve)
else()
    set(VE_CORE_LIBRARY ve)
endif()
set(VE_CORE_LIBRARY ${VE_CORE_LIBRARY} PARENT_SCOPE)

add_library(${VE_CORE_LIBRARY} SHARED
    ${_CORE_ALL}
    ${_PLAT_SOURCES}
)

target_compile_definitions(${VE_CORE_LIBRARY} PRIVATE VE_LIBRARY)

# PUBLIC includes — consumers get ve/global.h, ve/core/base.h, etc.
target_include_directories(${VE_CORE_LIBRARY} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# PRIVATE includes — platform internals
target_include_directories(${VE_CORE_LIBRARY} PRIVATE
    ${_PLAT_DIR}
)

# --- deps ---
ve_find_package(yaml-cpp
    GIT_REPO  https://github.com/jbeder/yaml-cpp.git
    GIT_TAG   0.8.0
    OPTIONS
        YAML_CPP_BUILD_TOOLS   OFF
        YAML_CPP_BUILD_CONTRIB ON
        YAML_BUILD_SHARED_LIBS OFF
        YAML_CPP_INSTALL       OFF
)
ve_find_package(simdjson
    GIT_REPO  https://github.com/simdjson/simdjson.git
    GIT_TAG   v3.12.2
    OPTIONS
        SIMDJSON_BUILD_STATIC_LIB ON
)
target_link_libraries(${VE_CORE_LIBRARY} PUBLIC  yaml-cpp::yaml-cpp)
target_link_libraries(${VE_CORE_LIBRARY} PRIVATE simdjson::simdjson)
target_link_libraries(${VE_CORE_LIBRARY} PRIVATE ve_dep_spdlog)
target_link_libraries(${VE_CORE_LIBRARY} PRIVATE ve_dep_asio)
target_link_libraries(${VE_CORE_LIBRARY} PRIVATE ve_dep_asio2)

message(STATUS "")
