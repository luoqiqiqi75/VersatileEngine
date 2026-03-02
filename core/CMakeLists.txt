# --- Qt MOC/UIC/RCC ---
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# --- Gather sources ---
file(GLOB_RECURSE LIB_INCLUDE_FILES ${VE_CORE_LIBRARY_INCLUDE}/*.h)
ve_find_sources(${CMAKE_CURRENT_SOURCE_DIR} LIB_SOURCES)
ve_find_resources(${CMAKE_CURRENT_SOURCE_DIR} UI_FILES QRC_FILES TS_FILES)

# --- Platform-specific sources ---
set(LIB_PLATFORM_INCLUDE_DIR)
if(WIN32)
    set(LIB_PLATFORM_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/platform/win)
elseif(UNIX AND NOT APPLE AND NOT ANDROID)
    set(LIB_PLATFORM_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/platform/linux)
else()
    set(LIB_PLATFORM_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/platform/unsupported)
endif()

file(GLOB_RECURSE LIB_PLATFORM_SOURCES
    ${LIB_PLATFORM_INCLUDE_DIR}/*.h
    ${LIB_PLATFORM_INCLUDE_DIR}/*.cpp
)

# --- Library target ---
add_library(${VE_CORE_LIBRARY}
    ${LIB_INCLUDE_FILES}
    ${LIB_SOURCES}
    ${LIB_PLATFORM_SOURCES}
    ${UI_FILES} ${QRC_FILES} ${TS_FILES}
)

target_compile_definitions(${VE_CORE_LIBRARY} PRIVATE
    VE_LIBRARY
    CORE_LIBRARY
)

target_include_directories(${VE_CORE_LIBRARY} PRIVATE
    ${VE_CORE_LIBRARY_INCLUDE}
    ${LIB_PLATFORM_INCLUDE_DIR}
)

# Qt
ve_target_link_qt_components(${VE_CORE_LIBRARY} "${VE_QT_COMPONENTS}")

# temporary: imol internal headers
target_include_directories(${VE_CORE_LIBRARY} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ve/core/imol/
)

# --- 3rdparty: spdlog ---
target_link_libraries(${VE_CORE_LIBRARY} PRIVATE ve_3rdparty_spdlog)

# --- Platform-specific libraries ---
if(UNIX AND NOT APPLE AND NOT ANDROID)
    target_link_libraries(${VE_CORE_LIBRARY} PRIVATE dl)
endif()