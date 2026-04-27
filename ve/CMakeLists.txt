# =============================================================================
# ve/CMakeLists.txt - pure C++ base library -> libve / libve.dll
#
# Provides: ve/global.h (VE_API), ve/core/base.h (Object, Manager),
#           ve/core/log.h,
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
ve_resolve_library_target_name(VE_CORE_LIBRARY ve)
set(VE_CORE_LIBRARY ${VE_CORE_LIBRARY} PARENT_SCOPE)

add_library(${VE_CORE_LIBRARY} SHARED
    ${_CORE_ALL}
    ${_PLAT_SOURCES}
)

ve_add_core_library_alias(${VE_CORE_LIBRARY})

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
target_link_libraries(${VE_CORE_LIBRARY} PRIVATE ve_dep_simdjson)
target_link_libraries(${VE_CORE_LIBRARY} PRIVATE ve_dep_spdlog)
target_link_libraries(${VE_CORE_LIBRARY} PRIVATE ve_dep_asio)
target_link_libraries(${VE_CORE_LIBRARY} PRIVATE ve_dep_asio2)
target_link_libraries(${VE_CORE_LIBRARY} PRIVATE ve_dep_pugixml)
target_link_libraries(${VE_CORE_LIBRARY} PRIVATE ve_dep_msgpack)

# OpenSSL (required by asio_impl.cpp SSL symbols on Linux)
if(UNIX AND NOT APPLE)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(${VE_CORE_LIBRARY} PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

# dynamic library loading (dlopen on Linux)
if(NOT WIN32)
    target_link_libraries(${VE_CORE_LIBRARY} PRIVATE ${CMAKE_DL_LIBS})
endif()

# --- Version script: hide asio/spdlog/simdjson symbols from dynamic symbol table ---
# Prevents ODR conflicts when libve.so coexists with other .so files that also
# embed asio (e.g. libfastrtps.so in ROS2 Foxy). Only ve:: symbols are exported.
if(UNIX AND NOT APPLE)
    set(_VE_VERSION_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/libve.version")
    target_link_options(${VE_CORE_LIBRARY} PRIVATE
        "LINKER:--version-script=${_VE_VERSION_SCRIPT}"
    )
    set_target_properties(${VE_CORE_LIBRARY} PROPERTIES
        LINK_DEPENDS "${_VE_VERSION_SCRIPT}"
    )
endif()

# --- Install (when VE_INSTALL is ON, see top-level ve_install.cmake) ---
if(VE_INSTALL)
    install(TARGETS ${VE_CORE_LIBRARY}
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        INCLUDES DESTINATION include
    )
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
        DESTINATION include
        FILES_MATCHING PATTERN "*.h"
    )
endif()

message(STATUS "")
