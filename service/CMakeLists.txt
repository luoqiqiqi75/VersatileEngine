# --- Sources ---
file(GLOB_RECURSE LIB_SOURCES src/*.h src/*.hpp src/*.c src/*.cpp include/*.h include/*.hpp)

# --- Compiler options ---
if(MSVC)
    add_compile_options(/bigobj)
endif()

# --- Library target ---
add_library(${VE_SERVICE_LIBRARY}
    ${LIB_SOURCES}
)

target_compile_definitions(${VE_SERVICE_LIBRARY} PRIVATE VE_LIBRARY)

target_include_directories(${VE_SERVICE_LIBRARY} PUBLIC
    ${VE_CORE_LIBRARY_INCLUDE}
    ${VE_SERVICE_LIBRARY_INCLUDE}
)

# --- Qt (needed because core headers include Qt headers) ---
set(CMAKE_AUTOMOC ON)
ve_target_link_qt_components(${VE_SERVICE_LIBRARY} "${VE_QT_COMPONENTS}")

# --- 3rdparty: asio2 (includes asio, spdlog, cereal, fmt) ---
target_link_libraries(${VE_SERVICE_LIBRARY} PUBLIC ve_3rdparty_asio2)

target_link_libraries(${VE_SERVICE_LIBRARY} PRIVATE
    ${VE_CORE_LIBRARY}
)