# --- Qt MOC/UIC/RCC ---
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# --- Sources ---
file(GLOB_RECURSE TEST_SOURCES src/*.h src/*.cpp)

add_executable(test1
    ${TEST_SOURCES}
)

target_link_libraries(test1 PRIVATE
    ${VE_CORE_LIBRARY}
    ${VE_SERVICE_LIBRARY}
)
ve_target_link_qt_components(test1 "${VE_QT_COMPONENTS}")

target_include_directories(test1 PRIVATE
    ${VE_CORE_LIBRARY_INCLUDE}
    ${VE_SERVICE_LIBRARY_INCLUDE}
)