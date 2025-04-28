set(LIB_QT_COMPONENTS
    Core
    Network
    Widgets
)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 REQUIRED COMPONENTS ${LIB_QT_COMPONENTS})

file(GLOB_RECURSE LIB_INCLUDE_FILES ${VE_CORE_LIBRARY_INCLUDE}/*.h)
ve_find_sources(${CMAKE_CURRENT_SOURCE_DIR} LIB_SOURCES)
ve_find_resources(${CMAKE_CURRENT_SOURCE_DIR} UI_FILES QRC_FILES TS_FILES)

set(LIB_PLATFORM_INCLUDE_DIR )
set(LIB_PLATFORM_SOURCES )
if (WIN32)
        set(LIB_PLATFORM_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/win)
        file(GLOB_RECURSE LIB_PLATFORM_SOURCES
                ${CMAKE_CURRENT_SOURCE_DIR}/win/*.h
                ${CMAKE_CURRENT_SOURCE_DIR}/win/*.cpp
        )
endif()

add_compile_definitions(CORE_LIBRARY) #temp
add_compile_definitions(BWAF_LIBRARY) #temp

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