set(LIB_NAME lib${VE_NAMESPACE}core)
set(LIB_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include)

set(LIB_QT_COMPONENTS
    Core
    Network
    Widgets
)

find_package(Qt5 REQUIRED COMPONENTS ${LIB_QT_COMPONENTS} LinguistTools)

file(GLOB_RECURSE LIB_INCLUDE_FILES ${LIB_INCLUDE}/*.h)
ve_find_sources(${CMAKE_CURRENT_SOURCE_DIR} LIB_SOURCES)
ve_find_resources(${CMAKE_CURRENT_SOURCE_DIR} UI_FILES QRC_FILES TS_FILES)

add_compile_definitions(CORE_LIBRARY) #temp
add_compile_definitions(BWAF_LIBRARY) #temp

add_compile_definitions(VE_LIBRARY)
add_library(${LIB_NAME} SHARED ${LIB_INCLUDE_FILES} ${LIB_SOURCES} ${UI_FILES} ${QRC_FILES} ${TS_FILES} src/data.cpp include/ve/core/terminal.h src/terminal.cpp)

target_include_directories(${LIB_NAME} PRIVATE ${LIB_INCLUDE})
ve_target_link_qt_components(${LIB_NAME} "${LIB_QT_COMPONENTS}")

# temporary
target_include_directories(libvecore PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/ve/core/imol/
        )