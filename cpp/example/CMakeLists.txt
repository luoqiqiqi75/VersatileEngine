# =============================================================================
# cpp/example/CMakeLists.txt — full-stack VE example (links all available libs)
# =============================================================================

message(STATUS "")
message(STATUS "[ve_cpp_example]  full-stack example programs")

file(GLOB _EXAMPLE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/example_*.cpp)
file(GLOB _CONFIG_FILES    ${CMAKE_CURRENT_SOURCE_DIR}/*.json)

foreach(_SRC ${_EXAMPLE_SOURCES})
    get_filename_component(_NAME ${_SRC} NAME_WE)
    add_executable(${_NAME} ${_SRC})

    target_link_libraries(${_NAME} PRIVATE ${VE_CORE_LIBRARY})

    if(TARGET ${VE_DDS_LIBRARY})
        target_link_libraries(${_NAME} PRIVATE ${VE_DDS_LIBRARY})
    endif()

    if(UNIX)
        target_link_libraries(${_NAME} PRIVATE pthread dl)
    endif()

    foreach(_CFG ${_CONFIG_FILES})
        get_filename_component(_FNAME ${_CFG} NAME)
        add_custom_command(TARGET ${_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_CFG}" "$<TARGET_FILE_DIR:${_NAME}>/${_FNAME}")
    endforeach()
endforeach()

message(STATUS "")
