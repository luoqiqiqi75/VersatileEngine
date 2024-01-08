macro(ve_target_link_qt_components VE_IN_TARGET VE_IN_QT_COMPONENTS)
    foreach(QT_COMPONENT ${VE_IN_QT_COMPONENTS})
        target_link_libraries(${VE_IN_TARGET} PRIVATE Qt::${QT_COMPONENT})
    endforeach()
endmacro()

macro(ve_find_sources VE_IN_DIR VE_OUT_SOURCES)
    file(GLOB_RECURSE ${VE_OUT_SOURCES}
        ${VE_IN_DIR}/src/*.h
        ${VE_IN_DIR}/src/*.hpp
        ${VE_IN_DIR}/src/*.c
        ${VE_IN_DIR}/src/*.cpp
    )
endmacro()
macro(ve_find_resources VE_IN_DIR VE_OUT_UI_FILES VE_OUT_QRC_FILES VE_OUT_TS_FILES)
    file(GLOB_RECURSE ${VE_OUT_UI_FILES} ${VE_IN_DIR}/src/*.ui)
    file(GLOB_RECURSE ${VE_OUT_QRC_FILES} ${VE_IN_DIR}/asset/*.qrc)
    file(GLOB_RECURSE ${VE_OUT_TS_FILES} ${VE_IN_DIR}/language/*.ts)
endmacro()

macro(ve_add_qt5_lib VE_IN_DIR VE_IN_LIB_NAME) # VE_IN_SHARED VE_IN_QT_COMPONENTS
    if(EXISTS "${VE_IN_DIR}/${VE_IN_LIB_NAME}/CMakeLists.txt")
        add_subdirectory(${VE_IN_LIB_NAME})
    else()
        
    endif()
endmacro()