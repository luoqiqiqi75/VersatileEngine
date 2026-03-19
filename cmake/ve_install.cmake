# =============================================================================
# ve_install.cmake — Install rules for VersatileEngine (when VE_INSTALL=ON)
# =============================================================================
# Installs: libve, public headers, and CMake config for find_package(VersatileEngine).
# Consumers: target_link_libraries(mytarget PRIVATE VersatileEngine::ve)
# No EXPORT used so we avoid exporting build-tree deps (ve_dep_asio etc.).
# =============================================================================

# --- Config version (so find_package(VersatileEngine 2.0) works) ---
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/VersatileEngineConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/VersatileEngineConfigVersion.cmake"
    DESTINATION lib/cmake/VersatileEngine
)

# --- Config.cmake (creates VersatileEngine::ve imported target) ---
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VersatileEngineConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/VersatileEngineConfig.cmake"
    INSTALL_DESTINATION lib/cmake/VersatileEngine
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/VersatileEngineConfig.cmake"
    DESTINATION lib/cmake/VersatileEngine
)
