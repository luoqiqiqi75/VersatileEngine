file(GLOB_RECURSE LIB_SOURCES src/*.h src/*.hpp src/*.c src/*.cpp include/*.h include/*.hpp)

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Network) # temp

add_subdirectory(3rdparty/asio2)
set(ASIO_INCLUDE_DIRS
        3rdparty/asio2/include
        3rdparty/asio2/3rd
)

add_compile_definitions(VE_LIBRARY)

if (MSVC)
    add_compile_options(
            /bigobj
    )
endif ()

add_library(${VE_SERVICE_LIBRARY} SHARED
        ${LIB_SOURCES}
)

target_include_directories(${VE_SERVICE_LIBRARY} PUBLIC
        ${VE_CORE_LIBRARY_INCLUDE}
        ${VE_SERVICE_LIBRARY_INCLUDE}
        ${ASIO_INCLUDE_DIRS}
)

target_link_libraries(${VE_SERVICE_LIBRARY} PRIVATE
        ${VE_CORE_LIBRARY}
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Network # temp
)