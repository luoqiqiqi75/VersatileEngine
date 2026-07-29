# =============================================================================
# qt/test/CMakeLists.txt — veqt_test executable (Qt loop + timer scheduling)
# =============================================================================

message(STATUS "")
message(STATUS "[veqt_test]  Qt unit tests")

file(GLOB_RECURSE _QT_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(veqt_test ${_QT_TEST_SOURCES})

# ve_test.h is shared with the core test target
target_include_directories(veqt_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/ve/test
)

target_link_libraries(veqt_test PRIVATE ${VE_QT_LIBRARY})
ve_target_link_qt(veqt_test PRIVATE "Core")

if(UNIX AND NOT ANDROID)
    target_link_libraries(veqt_test PRIVATE pthread dl)
elseif(ANDROID)
    target_link_libraries(veqt_test PRIVATE dl)
endif()

message(STATUS "")
