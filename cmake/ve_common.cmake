# =============================================================================
# ve_common.cmake - Common CMake utilities for VersatileEngine
# =============================================================================

# --- Collect all source/resource files for a module directory ---
# Convention:
#   include/*.h      — public headers
#   src/*.h/cpp      — private sources (+ .ui files)
#   res/*.qrc/*.ts   — Qt resources & translations
#
# Sets: ${PREFIX}_INCLUDES, ${PREFIX}_SOURCES, ${PREFIX}_UI, ${PREFIX}_QRC,
#       ${PREFIX}_TS, ${PREFIX}_ALL
macro(ve_collect VE_IN_DIR VE_OUT_PREFIX)
    file(GLOB_RECURSE ${VE_OUT_PREFIX}_INCLUDES  ${VE_IN_DIR}/include/*.h)
    file(GLOB_RECURSE ${VE_OUT_PREFIX}_SOURCES
        ${VE_IN_DIR}/src/*.h
        ${VE_IN_DIR}/src/*.hpp
        ${VE_IN_DIR}/src/*.c
        ${VE_IN_DIR}/src/*.cpp
    )
    file(GLOB_RECURSE ${VE_OUT_PREFIX}_UI   ${VE_IN_DIR}/src/*.ui)
    file(GLOB_RECURSE ${VE_OUT_PREFIX}_QRC  ${VE_IN_DIR}/res/*.qrc)
    file(GLOB_RECURSE ${VE_OUT_PREFIX}_TS   ${VE_IN_DIR}/res/*.ts)
    # _ALL: all compilable / processable files + public headers.
    # Public headers are included so AUTOMOC can find Q_OBJECT classes
    # and generate the corresponding moc_*.cpp files.
    set(${VE_OUT_PREFIX}_ALL
        ${${VE_OUT_PREFIX}_INCLUDES}
        ${${VE_OUT_PREFIX}_SOURCES}
        ${${VE_OUT_PREFIX}_UI}
        ${${VE_OUT_PREFIX}_QRC}
        ${${VE_OUT_PREFIX}_TS}
    )
endmacro()

# --- Link Qt components to a target ---
# Usage: ve_target_link_qt(target VISIBILITY "Core;Network;Widgets")
macro(ve_target_link_qt VE_IN_TARGET VE_IN_VISIBILITY VE_IN_COMPONENTS)
    foreach(_qt_comp ${VE_IN_COMPONENTS})
        target_link_libraries(${VE_IN_TARGET} ${VE_IN_VISIBILITY} Qt${QT_VERSION_MAJOR}::${_qt_comp})
    endforeach()
endmacro()
