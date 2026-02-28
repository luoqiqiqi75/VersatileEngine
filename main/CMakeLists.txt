cmake_minimum_required(VERSION 3.14)

project(test1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LIB_QT_COMPONENTS
        Core
        Network
        Widgets
)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS ${LIB_QT_COMPONENTS})
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS ${LIB_QT_COMPONENTS})

file(GLOB_RECURSE TEST_SOURCES src/*.h src/*.cpp)

add_executable(test1
        ${TEST_SOURCES}
)
target_link_libraries(test1
        ${VE_CORE_LIBRARY}
        ${VE_SERVICE_LIBRARY}

        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Network
)
target_include_directories(test1 PRIVATE
        ${VE_CORE_LIBRARY_INCLUDE}
        ${VE_SERVICE_LIBRARY_INCLUDE}
)

#include(GNUInstallDirs)
#install(TARGETS test1
#    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
#    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
#)
