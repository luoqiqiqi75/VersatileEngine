# =============================================================================
# qt/program/browser/CMakeLists.txt - veqtbrowser module DLL
# =============================================================================
#
# VE_QT_BROWSER_DEPLOY_WEBAPP (default ON): POST_BUILD copies webapp/ + veservice.js next to the DLL.

option(VE_QT_BROWSER_DEPLOY_WEBAPP "Copy built-in webapp/ next to veqtbrowser on each build" ON)

set(_BROWSER_SOURCES
    src/BrowserModule.cpp
    src/BrowserModule.h
    res/browser.qrc
)

file(GLOB_RECURSE _BROWSER_QML qml/*.qml)

add_library(veqtbrowser SHARED ${_BROWSER_SOURCES} ${_BROWSER_QML})

target_link_libraries(veqtbrowser PRIVATE ${VE_QT_LIBRARY})
ve_target_link_qt(veqtbrowser PRIVATE "Quick;WebView")

target_include_directories(veqtbrowser PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(VE_QT_BROWSER_DEPLOY_WEBAPP)
    add_custom_command(
        TARGET veqtbrowser POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/browser.json
            $<TARGET_FILE_DIR:veqtbrowser>/browser.json
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/webapp
            $<TARGET_FILE_DIR:veqtbrowser>/webapp
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/ve/js/veservice.js
            $<TARGET_FILE_DIR:veqtbrowser>/webapp/veservice.js
        COMMENT "Copy browser.json, webapp/ and veservice.js next to veqtbrowser"
    )
else()
    add_custom_command(
        TARGET veqtbrowser POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/browser.json
            $<TARGET_FILE_DIR:veqtbrowser>/browser.json
        COMMENT "Copy browser.json (VE_QT_BROWSER_DEPLOY_WEBAPP=OFF: not syncing webapp/)"
    )
endif()
