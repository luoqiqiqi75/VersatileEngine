set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(LIB_QT_COMPONENTS
        Core
        Network
        Widgets
)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS ${LIB_QT_COMPONENTS})
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS ${LIB_QT_COMPONENTS})

file(GLOB_RECURSE LIB_INCLUDE_FILES ${VE_CORE_LIBRARY_INCLUDE}/*.h)
ve_find_sources(${CMAKE_CURRENT_SOURCE_DIR} LIB_SOURCES)
ve_find_resources(${CMAKE_CURRENT_SOURCE_DIR} UI_FILES QRC_FILES TS_FILES)

set(LIB_PLATFORM_INCLUDE_DIR )
if (WIN32)
        set(LIB_PLATFORM_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/platform/win)
elseif (UNIX AND NOT APPLE AND NOT ANDROID)
        set(LIB_PLATFORM_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/platform/linux)
else ()
        set(LIB_PLATFORM_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/platform/unsupported)
endif()
file(GLOB_RECURSE LIB_PLATFORM_SOURCES
        ${LIB_PLATFORM_INCLUDE_DIR}/*.h
        ${LIB_PLATFORM_INCLUDE_DIR}/*.cpp
)

add_compile_definitions(CORE_LIBRARY) #temp

add_compile_definitions(VE_LIBRARY)
add_library(${VE_CORE_LIBRARY} SHARED
        ${LIB_INCLUDE_FILES}
        ${LIB_SOURCES}
        ${LIB_PLATFORM_SOURCES}
        ${UI_FILES} ${QRC_FILES} ${TS_FILES}
)

target_include_directories(${VE_CORE_LIBRARY} PRIVATE
        ${VE_CORE_LIBRARY_INCLUDE}
        ${LIB_PLATFORM_INCLUDE_DIR}
)
ve_target_link_qt_components(${VE_CORE_LIBRARY} "${LIB_QT_COMPONENTS}")

# temporary
target_include_directories(${VE_CORE_LIBRARY} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/ve/core/imol/
)
target_link_libraries(${VE_CORE_LIBRARY} PRIVATE spdlog)

# Linux平台特定设置：链接dl库用于dladdr函数
if (UNIX AND NOT APPLE AND NOT ANDROID)
    target_link_libraries(${VE_CORE_LIBRARY} PRIVATE dl)
endif()