# =============================================================================
# cpp/qt/veService/CMakeLists.txt — adds service sources to libveqt
# =============================================================================

# MSVC /bigobj — needed for asio2 template-heavy TUs.
# Must use target_compile_options since libveqt is defined in the parent directory.
if(MSVC)
    target_compile_options(${VE_QT_LIBRARY} PRIVATE /bigobj)
endif()

ve_collect(${CMAKE_CURRENT_SOURCE_DIR} _SERVICE)

target_sources(${VE_QT_LIBRARY} PRIVATE ${_SERVICE_ALL})

target_include_directories(${VE_QT_LIBRARY} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# deps: asio2 (includes asio, spdlog, cereal, fmt)
target_link_libraries(${VE_QT_LIBRARY} PRIVATE ve_dep_asio2)
