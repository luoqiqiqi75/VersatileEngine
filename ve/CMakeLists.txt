# =============================================================================
# ve/CMakeLists.txt - pure C++ base library -> libve / libve.dll
#
# Provides: ve/global.h (VE_API), ve/core/base.h (Object, Manager),
#           ve/core/log.h,
#           ve/core/factory.h, platform crash reporting
# =============================================================================

message(STATUS "")
message(STATUS "[libve]  pure C++ core")

ve_collect(${CMAKE_CURRENT_SOURCE_DIR} _CORE)

# --- Platform-specific sources ---
if(WIN32)
    set(_PLAT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/platform/win)
elseif(UNIX)
    set(_PLAT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/platform/posix)
else()
    set(_PLAT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/platform/unsupported)
endif()

file(GLOB_RECURSE _PLAT_SOURCES CONFIGURE_DEPENDS ${_PLAT_DIR}/*.h ${_PLAT_DIR}/*.cpp)

# --- Library target ---
ve_resolve_library_target_name(VE_CORE_LIBRARY ve)
set(VE_CORE_LIBRARY ${VE_CORE_LIBRARY} PARENT_SCOPE)

add_library(${VE_CORE_LIBRARY}
    ${_CORE_ALL}
    ${_PLAT_SOURCES}
)

ve_add_core_library_alias(${VE_CORE_LIBRARY})

# --- Embedded resources (command docs etc.) -> ve::res::read("ve/service/op.json") ---
ve_collect_resources(${VE_CORE_LIBRARY} ${CMAKE_CURRENT_SOURCE_DIR} PREFIX ve)

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

if(WIN32)
    target_link_libraries(${VE_CORE_LIBRARY} PRIVATE dbghelp)
elseif(UNIX)
    # libunwind can start directly from sigaction's ucontext, unlike
    # backtrace()/_Unwind_Backtrace() which start inside the rescue handler.
    find_path(_VE_LIBUNWIND_INCLUDE_DIR libunwind.h)
    find_library(_VE_LIBUNWIND_LIBRARY NAMES unwind)

    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _VE_PROCESSOR)
    if(_VE_PROCESSOR MATCHES "^(x86_64|amd64)$")
        find_library(_VE_LIBUNWIND_PLATFORM_LIBRARY NAMES unwind-x86_64)
    elseif(_VE_PROCESSOR MATCHES "^(aarch64|arm64)$")
        find_library(_VE_LIBUNWIND_PLATFORM_LIBRARY NAMES unwind-aarch64)
    elseif(_VE_PROCESSOR MATCHES "^(i[3-6]86|x86)$")
        find_library(_VE_LIBUNWIND_PLATFORM_LIBRARY NAMES unwind-x86)
    elseif(_VE_PROCESSOR MATCHES "^arm")
        find_library(_VE_LIBUNWIND_PLATFORM_LIBRARY NAMES unwind-arm)
    endif()

    if(_VE_LIBUNWIND_INCLUDE_DIR AND _VE_LIBUNWIND_LIBRARY)
        include(CheckCXXSourceCompiles)
        set(_VE_REQUIRED_INCLUDES_SAVE "${CMAKE_REQUIRED_INCLUDES}")
        set(_VE_REQUIRED_LIBRARIES_SAVE "${CMAKE_REQUIRED_LIBRARIES}")
        set(CMAKE_REQUIRED_INCLUDES "${_VE_LIBUNWIND_INCLUDE_DIR}")
        set(CMAKE_REQUIRED_LIBRARIES "${_VE_LIBUNWIND_LIBRARY}")
        if(_VE_LIBUNWIND_PLATFORM_LIBRARY)
            list(APPEND CMAKE_REQUIRED_LIBRARIES "${_VE_LIBUNWIND_PLATFORM_LIBRARY}")
        endif()
        check_cxx_source_compiles([[
            #define UNW_LOCAL_ONLY
            #include <libunwind.h>
            int main() {
                unw_cursor_t cursor;
                unw_context_t context;
                return unw_init_local2(&cursor, &context, UNW_INIT_SIGNAL_FRAME);
            }
        ]] VE_RESCUE_HAS_LIBUNWIND)
        set(CMAKE_REQUIRED_INCLUDES "${_VE_REQUIRED_INCLUDES_SAVE}")
        set(CMAKE_REQUIRED_LIBRARIES "${_VE_REQUIRED_LIBRARIES_SAVE}")
    endif()

    if(VE_RESCUE_HAS_LIBUNWIND)
        target_compile_definitions(${VE_CORE_LIBRARY} PRIVATE VE_RESCUE_HAS_LIBUNWIND=1)
        target_include_directories(${VE_CORE_LIBRARY} PRIVATE "${_VE_LIBUNWIND_INCLUDE_DIR}")
        target_link_libraries(${VE_CORE_LIBRARY} PRIVATE "${_VE_LIBUNWIND_LIBRARY}")
        if(_VE_LIBUNWIND_PLATFORM_LIBRARY)
            target_link_libraries(${VE_CORE_LIBRARY} PRIVATE "${_VE_LIBUNWIND_PLATFORM_LIBRARY}")
        endif()
        message(STATUS "Crash stack unwinder: libunwind (signal context)")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # Dependency-free fallback walks frame records from ucontext. Preserve
        # them in libve even in optimized builds.
        target_compile_options(${VE_CORE_LIBRARY} PRIVATE -fno-omit-frame-pointer)
        message(STATUS "Crash stack unwinder: frame-pointer fallback")
    endif()
endif()

# dynamic library loading (dlopen on Linux)
if(NOT WIN32)
    target_link_libraries(${VE_CORE_LIBRARY} PRIVATE ${CMAKE_DL_LIBS})
endif()

# --- Version script: hide asio/spdlog/simdjson symbols from dynamic symbol table ---
# Prevents ODR conflicts when libve.so coexists with other .so files that also
# embed asio (e.g. libfastrtps.so in ROS2 Foxy). Only ve:: symbols are exported.
if(UNIX AND NOT APPLE AND NOT ANDROID)
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
