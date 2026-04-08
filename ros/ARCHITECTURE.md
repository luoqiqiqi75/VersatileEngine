# ve/ros Architecture

`ve/ros` is the official ROS-facing layer for VersatileEngine.

It is intentionally split into two concerns:

1. Core ROS integration
2. Project-specific protocol adapters

## Core ROS Integration

The core layer belongs in `ve/ros` and should stay stable across projects.

Responsibilities:

- backend registry and runtime selection
- ROS environment inspection and discovery helpers
- official `RosModule` commands such as `ros.info`, `ros.backend.*`, `ros.env`
- typed topic publish/subscribe support
- typed service support and `ve::command` exposure
- parser registry for common payload formats

This layer should eventually support multiple backends:

- `rclcpp` as the primary ROS2 backend
- native Fast DDS as a performance-oriented backend

## Project Adapters

Project-specific adapters should not live in the `ve/ros` core.

Examples:

- string payloads that contain JSON or YAML
- vendor-specific command naming
- legacy ROS topics and services with inconsistent schemas
- custom translation from ROS messages to a project's VE node model

These adapters should plug into `ve/ros` through parser registration and
backend-neutral command/topic/service interfaces.

## Current Incremental Refactor

This repository currently ships:

- backend-neutral public headers in `include/ve/ros/*.h`
- a payload parser registry in `ve/ros/parser.h`
- a runtime/backend registry in `ve/ros/backend.h` and `ve/ros/runtime.h`
- a primary `rclcpp` backend in `src/backend/rclcpp`
- a secondary Fast DDS backend in `src/backend/fastdds`
- a single official `RosModule` in `src/module/ros_module.cpp`

Legacy `data.h`, `veRos`, `veFastDDS`, and public `ve/ros/dds/*` entry points
have been removed from the public surface.
