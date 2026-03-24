# ROS / FastDDS 集成指南

## 概述

`libveros` (以前 libvedds) 提供 VE Node 与 ROS/DDS 的无缝双向同步：
- **Node <-> DDS Topic**：使用动态类型 (Dynamic Types) 自动映射。
- **Command <-> DDS Service**：VE 命令暴露为 DDS 服务，兼容 ROS2 客户端。
- **rqt 对接**：通过共享 DDS domain 或 ROS2 bridge 节点实现。

库结构已整理为类似 `qt/`：
- `ros/include/ve/ros/` — 公共 API
- `ros/src/` — 实现 (dds/, service/, module/, data/)
- `ros/program/` — 示例模块

## 快速开始

1. 确保安装 FastDDS (fastrtps + fastcdr)。
2. 在 `ve.json` 配置：

```json
{
  "ve": {
    "ros": {
      "config": {
        "domain_id": 0,
        "service_prefix": "ve",
        "command_service": true
      }
    }
  }
}
```

3. 加载模块 (默认自动) 或作为 plugin。

## 使用 Bridge 同步 Node

```cpp
#include "ve/ros/dds/bridge.h"
#include "ve/ros/dds/participant.h"

auto& p = ve::dds::Participant::instance(0);
ve::dds::Bridge bridge(ve::node::root(), p);

// VE -> ROS/DDS
bridge.expose("robot/imu", "rt/imu");        // NODE_CHANGED 自动发布

// ROS/DDS -> VE
bridge.subscribe("rt/lidar", "sensor/lidar");
```

## CommandService

```cpp
ve::dds::CommandService svc(p, "ve");
svc.start();  // 注册 ve/command, ve/data_get 等服务
```

使用 YAML 序列化 Node/Var，支持 rqt 或 ros2 service call。

## rqt 对接方案

**推荐方式** (DDS domain 共享)：

- 设置相同 `ROS_DOMAIN_ID`。
- 使用 `Bridge` 暴露关键子树 (`ve/ros/...`)。
- rqt 修改 ROS topic 时，DDS 变化通过 `DynSubscriber` 更新 VE Node。
- 反之，VE Node 变化通过 `DynPublisher` 同步到 ROS。

**替代**：创建外部 ROS2 ament 包作为 bridge node，使用 rclcpp 订阅标准 msg/srv，通过 VE 的 HTTP/WS/TBS 或 TCP Binary Service 与 VE 通信。

## 配置选项

- `ve/ros/config/domain_id`
- `ve/ros/config/service_prefix`
- `ve/ros/config/command_service`

## 命令

内置命令如 `ros_demo.heartbeat` 可通过 DDS 服务调用。

## 更多

见 `ros/src/module/ros_demo_module.cpp` 示例和 `ve/test/` 中的相关测试。

更新于 2026-03-24。
