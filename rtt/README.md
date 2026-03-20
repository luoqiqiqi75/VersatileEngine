# cpp/rtt — libvertt

xcore IMOL 框架移植，纯 C++17，无 Qt 依赖。构建产出单一静态库 `libvertt`。

## 目录结构

```
cpp/rtt/
├── CMakeLists.txt          # 创建 libvertt target
├── veRttCore/              # 所有基础实现
│   ├── CMakeLists.txt      # ve_collect → target_sources(libvertt)
│   ├── include/ve/rtt/     # 公开头文件
│   │   ├── ve_rtt_global.h # 全局宏与 includes
│   │   ├── meta.h          # FInfo/TList/Meta 模板元编程
│   │   ├── container.h     # Vector/List/Map/HashMap/Dict/Values
│   │   ├── object.h        # Object(signal/slot) + Manager + Creator
│   │   ├── data_object.h   # TDataObject<T>, JsonInterface
│   │   ├── json_ref.h      # Json (nlohmann) + JsonRef 路径导航器
│   │   ├── global_data.h   # 全局数据注册 + IMOL_DEC_G_DATA 宏
│   │   ├── result.h        # Result 统一返回类型
│   │   ├── procedure.h     # Procedure 函数包装器
│   │   ├── command_object.h# CommandObject 命令执行单元
│   │   ├── cip.h           # CIP 输入预处理
│   │   ├── loop_object.h   # LoopObject + PoolLoopObject
│   │   ├── evpp_loop.h     # EvppLoopObject (需 IMOL_HAS_EVPP)
│   │   ├── rtt_loop.h      # RttLoopObject (需 IMOL_HAS_OROCOS_RTT)
│   │   ├── loop_manager.h  # loop::mgr() + 全局 loop 指针
│   │   ├── net_object.h    # NetObject + 消息策略 (RAW/CACHE/BACKSLASH_R)
│   │   ├── server_net_object.h # ServerNetObject + EvppServerNetObject
│   │   ├── client_net_object.h # ClientNetObject + EvppClientNetObject
│   │   └── net_factory.h   # net::createServer/createClient
│   └── src/                # 实现文件
│       ├── meta.cpp         container.cpp      object.cpp
│       ├── json_ref.cpp     result.cpp         command_object.cpp
│       ├── cip.cpp          loop_object.cpp    evpp_loop.cpp
│       ├── rtt_loop.cpp     net_object.cpp     server_net_object.cpp
│       └── client_net_object.cpp  net_factory.cpp
└── XService/               # 对外服务层
    ├── CMakeLists.txt
    ├── include/ve/rtt/
    │   ├── xservice_error.h     # 错误码
    │   ├── sdk_service.h        # JSON 协议 (g/s/c)
    │   └── sys_socket_service.h # 文本协议
    └── src/
        ├── sdk_service.cpp
        └── sys_socket_service.cpp
```

## 构建

`libvertt` 始终参与构建（纯 C++，无外部硬依赖）。

```cmake
option(VE_BUILD_RTT "Build cpp/rtt (header-only IMOL xcore modules)" ON)
```

## 条件编译

| 宏 | 启用条件 | 影响 |
|----|---------|------|
| `IMOL_HAS_EVPP` | `find_package(evpp)` 成功 | EvppLoopObject, EvppServer/Client |
| `IMOL_HAS_OROCOS_RTT` | `find_package(OROCOS-RTT)` 成功 | RttLoopObject |

无上述依赖时，核心功能仍可用（PoolLoopObject 线程池、RawMsgHandler 等）。

## Include 路径

```cpp
#include <ve/rtt/object.h>
#include <ve/rtt/command_object.h>
#include <ve/rtt/sdk_service.h>
```

## 来源

移植自 xcore (2023)，保留 `imol::` 命名空间。
详见 `docs/internal/history/xcore/`。
