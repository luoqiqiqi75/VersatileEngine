cmake_minimum_required(VERSION 3.14)

project(test1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt5 REQUIRED COMPONENTS Core Widgets Network)

add_executable(test1
  test.cpp
)
target_link_libraries(test1 libvecore Qt5::Core Qt5::Widgets Qt5::Network)
target_include_directories(test1 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../core/include)

include(GNUInstallDirs)
install(TARGETS test1
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
