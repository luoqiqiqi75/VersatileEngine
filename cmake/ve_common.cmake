# =============================================================================
# ve_common.cmake - Common CMake utilities for VersatileEngine
# =============================================================================

# --- Qt helper: link Qt components to a target ---
macro(ve_target_link_qt_components VE_IN_TARGET VE_IN_QT_COMPONENTS)
    foreach(QT_COMPONENT ${VE_IN_QT_COMPONENTS})
        target_link_libraries(${VE_IN_TARGET} PRIVATE Qt${QT_VERSION_MAJOR}::${QT_COMPONENT})
    endforeach()
endmacro()

# --- Source file discovery ---
macro(ve_find_sources VE_IN_DIR VE_OUT_SOURCES)
    file(GLOB_RECURSE ${VE_OUT_SOURCES}
        ${VE_IN_DIR}/src/*.h
        ${VE_IN_DIR}/src/*.hpp
        ${VE_IN_DIR}/src/*.c
        ${VE_IN_DIR}/src/*.cpp
    )
endmacro()

# --- Resource file discovery (Qt UI / QRC / TS) ---
macro(ve_find_resources VE_IN_DIR VE_OUT_UI_FILES VE_OUT_QRC_FILES VE_OUT_TS_FILES)
    file(GLOB_RECURSE ${VE_OUT_UI_FILES} ${VE_IN_DIR}/src/*.ui)
    file(GLOB_RECURSE ${VE_OUT_QRC_FILES} ${VE_IN_DIR}/asset/*.qrc)
    file(GLOB_RECURSE ${VE_OUT_TS_FILES} ${VE_IN_DIR}/language/*.ts)
endmacro()

# --- Add a VE library from a subdirectory ---
# Sets output variables for library target name and include path,
# then processes the subdirectory's CMakeLists.txt.
macro(ve_add_library VE_IN_RELATIVE_DIR VE_OUT_LIB_NAME VE_OUT_LIB_INCLUDE)
    set(LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/${VE_IN_RELATIVE_DIR})
    string(REPLACE "/" "" LIB_NAME ${VE_NAMESPACE}${VE_IN_RELATIVE_DIR})

    if(WIN32)
        set(${VE_OUT_LIB_NAME} lib${LIB_NAME})
    else()
        set(${VE_OUT_LIB_NAME} ${LIB_NAME})
    endif()

    set(${VE_OUT_LIB_INCLUDE} ${LIB_DIR}/include)

    if(EXISTS "${LIB_DIR}/CMakeLists.txt")
        add_subdirectory(${LIB_DIR})
    else()
        message(WARNING "CMakeLists.txt not found in ${LIB_DIR}, skipping.")
    endif()
endmacro()
