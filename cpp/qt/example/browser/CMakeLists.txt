# =============================================================================
# cpp/qt/example/browser/CMakeLists.txt — WebView browser application
# =============================================================================
#
# VE_QT_BROWSER_DEPLOY_WEBAPP (default ON): POST_BUILD copies webapp/ into the exe dir.

option(VE_QT_BROWSER_DEPLOY_WEBAPP "Copy built-in webapp/ next to veQtBrowser on each build" ON)

ve_collect(${CMAKE_CURRENT_SOURCE_DIR} _BROWSER)

add_executable(veQtBrowser ${_BROWSER_ALL})

target_link_libraries(veQtBrowser PRIVATE ${VE_QT_LIBRARY})

ve_target_link_qt(veQtBrowser PRIVATE "Quick;WebView")

if(VE_QT_BROWSER_DEPLOY_WEBAPP)
    add_custom_command(
        TARGET veQtBrowser POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/browser.json
            $<TARGET_FILE_DIR:veQtBrowser>/browser.json
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/webapp
            $<TARGET_FILE_DIR:veQtBrowser>/webapp
        COMMENT "Copy browser.json and webapp next to veQtBrowser"
    )
else()
    add_custom_command(
        TARGET veQtBrowser POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/browser.json
            $<TARGET_FILE_DIR:veQtBrowser>/browser.json
        COMMENT "Copy browser.json (VE_QT_BROWSER_DEPLOY_WEBAPP=OFF: not syncing webapp/)"
    )
endif()
