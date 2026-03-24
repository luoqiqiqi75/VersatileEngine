# =============================================================================
# ros/program/CMakeLists.txt — optional example / bridge modules for libveros
# Similar to qt/program/browser/
# =============================================================================

message(STATUS "  + ros/program (optional example modules)")

# Example: veros_demo module DLL that can be loaded via ve/entry/plugins
# add_library(veros_demo MODULE src/veros_demo.cpp)
# target_link_libraries(veros_demo PRIVATE ${VE_ROS_LIBRARY})
# etc.

# For now, keep ros_demo_module inside the library (registered via VE_REGISTER)
# Future: move demo to separate DLL if needed for dynamic loading without linking libveros
