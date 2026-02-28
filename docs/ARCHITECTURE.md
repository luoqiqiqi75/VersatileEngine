# VE (Versatile Engine) 架构总结与演进思路

## 目录

1. [现状总结](#1-现状总结)
2. [核心设计分析](#2-核心设计分析)
3. [当前架构的耦合问题](#3-当前架构的耦合问题)
4. [演进目标](#4-演进目标)
5. [重构方案](#5-重构方案)
6. [各端适配层设计](#6-各端适配层设计)
7. [从 Tree 到 Mesh](#7-从-tree-到-mesh)
8. [实施路线](#8-实施路线)

---

## 1. 现状总结

### 1.1 历史脉络

VE 的数据核心起源于 **imol**（`imol::ModuleObject`），最初基于 Qt 的 QObject + QVariant 体系构建。这是一个自然的选择——Qt 的元对象系统提供了：

- **QVariant** —— 万能值容器（类比动态类型），支持 int、double、QString、QVariantList、QVariantMap 以及自定义类型
- **QObject 信号槽** —— 原生的响应式机制（changed、added、removed 信号）
- **QMetaObject** —— 运行时类型内省
- **QMutex** —— 线程安全

因此 `ModuleObject` 天然获得了"带响应式信号的树节点"能力，并且 QVariant 让每个节点可以存储任意类型的值。

后来 ve 也需要跑在纯 C++ 后端（无 Qt 环境），于是出现了两套并行的对象系统：

| 层 | 类 | 依赖 | 用途 |
|---|---|---|---|
| 纯 C++ 对象 | `ve::Object` + `ve::Manager` | 仅 STL | 轻量对象树，自带 signal/action 机制 |
| Qt 数据节点 | `imol::ModuleObject`（别名 `ve::Data`） | Qt Core | 完整数据树，QVariant 值，Qt 信号槽 |

### 1.2 当前架构一览

```
┌─────────────────────────────────────────────────────────────┐
│                         ve 项目结构                          │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  core/                                                      │
│  ├── ve::Object          纯 C++ 对象基类（parent, signal）   │
│  ├── ve::Manager         对象容器（HashMap<name, Object*>）  │
│  ├── ve::Module          模块生命周期（NONE→INIT→READY→DEINIT）│
│  ├── ve::Factory<Sig>    工厂模式（Dict<name, function>）    │
│  ├── ve::Data            = imol::ModuleObject（Qt 数据节点） │
│  ├── ve::d("path")       全局路径访问器（自动创建节点）       │
│  ├── VE_D("path")        静态缓存访问器（高性能）            │
│  └── imol::*             原始实现（状态机、命令、日志等）     │
│                                                             │
│  service/                                                   │
│  ├── CBS (Server/Client) TCP 二进制协议（C++↔C++ IPC）       │
│  │   └── echo / publish / subscribe (single | recursive)    │
│  └── CommandServer       TCP 文本命令服务                    │
│                                                             │
│  (MozHMI 中，不在 ve 仓库内)                                 │
│  ├── WebSocket Server    JSON 协议（JS/QML 前端通信）        │
│  └── QuickNode           QML 桥接层（VEData/veNode）         │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 1.3 ModuleObject（ve::Data）的数据结构

这是 ve 的核心，一个**带值的树节点**：

```
ModuleObject
├── m_name: QString          节点名称
├── m_var: QVariant          节点值（任意类型）
├── m_pmobj: ModuleObject*   父节点指针
├── m_prev_bmobj             前兄弟指针 ─┐
├── m_next_bmobj             后兄弟指针 ─┤ 双向链表维护插入顺序
├── m_first_cmobj            首子节点   ─┤
├── m_last_cmobj             末子节点   ─┘
├── m_cmobjs: QHash<QString, ModuleObject*>  子节点哈希表（按名查找）
├── m_is_quiet: bool         静默模式（抑制信号）
├── m_is_watching: bool      监听模式（子树变化向上冒泡）
├── m_mutex: QMutex*         线程锁
│
├── signals:
│   ├── changed(new_var, old_var, changer)     值变化
│   ├── activated(changed_mobj, type, changer) 子树中任意节点变化（需 watch=true）
│   ├── added(rname, changer)                  子节点新增
│   ├── removed(rname, changer)                子节点移除
│   ├── gonnaInsert(target, ref, changer)      即将新增（可拦截）
│   └── gonnaRemove(target, ref, changer)      即将移除（可拦截）
│
└── 导航:
    ├── p(level)             向上 N 级父节点
    ├── c(rname) / c(index)  子节点（按名/按序号）
    ├── b(next)              兄弟节点（正=后，负=前）
    ├── r(rpath)             相对路径导航（"a.b.c"）
    └── fullName(ancestor)   从某祖先到自身的完整路径
```

**节点命名规则：**
- 命名节点：普通字符串，如 `"config"`、`"LeftArm"`
- 索引节点：以 `#` 开头，如 `"#0"`、`"#1"`（用于数组语义）
- 特殊节点：以 `_` 开头（导出时可被忽略）

**序列化能力：**
- JSON（importFromJson / exportToJson）
- XML（importFromXml / exportToXml）
- 二进制（importFromBin / exportToBin，支持 QVariant 自定义类型和压缩）
- QVariant（importFromVariant / exportToVariant）

### 1.4 各端接入方式

```
                    ┌─────────────────┐
                    │  ve::Data 树     │
                    │  (ModuleObject)  │
                    └───────┬─────────┘
                            │
            ┌───────────────┼───────────────┐
            │               │               │
     ┌──────▼──────┐ ┌─────▼──────┐ ┌──────▼──────┐
     │   C++ 直接   │ │  QML 桥接  │ │  JS WebSocket│
     │   ve::d()    │ │  QuickNode │ │  veservice.js│
     │   VE_D()     │ │  VEData {} │ │  get/set/sub │
     └─────────────┘ └────────────┘ └──────────────┘

     C++ 后端模块      Qt Quick UI      Web 前端 (React等)
```

---

## 2. 核心设计分析

### 2.1 ve 的本质——"响应式数据中间件"

剥离掉具体实现，ve 的核心设计可以归纳为：

```
一棵全局的、路径寻址的、带信号传播的数据树，
各模块通过 路径（path）读写数据、通过 信号（signal）响应变化，
模块之间不直接耦合，只通过"数据节点"间接通信。
```

这个模式的价值在于：

1. **解耦**：模块 A 写 `robot.state.power = 1`，模块 B 订阅 `robot.state.power` 的变化——A 和 B 互不知道对方存在
2. **结构化**：所有数据有组织地挂在一棵树上，天然分层分域
3. **可观测**：任何节点的变化都可以被监听，包括子树变化（`activated` + `watch`）
4. **跨进程/跨语言**：CBS 做 C++↔C++，WebSocket 做 C++↔JS/QML
5. **可序列化**：整棵树或子树可以导出为 JSON/XML/Binary

### 2.2 与其他体系的对比

| 概念 | ve | React/Zustand | 游戏引擎 | 文件系统 |
|------|-----|---------------|---------|---------|
| 节点 | ModuleObject | Store / State | GameObject / Node | File / Directory |
| 寻址 | 点分路径 `a.b.c` | selector 函数 | 场景树路径 | 斜杠路径 `/a/b/c` |
| 值存储 | QVariant | JS 对象 | Component | 文件内容 |
| 变化通知 | Qt signal (changed) | subscribe/selector | signal/event | inotify / FSWatcher |
| 子树通知 | activated + watch | ❌（需手动） | 冒泡事件 | 递归 watch |
| 新增/删除通知 | added / removed | ❌（需手动） | onChildAdded | inotify |
| 拦截 | gonnaInsert/Remove | ❌ | 部分引擎有 | ❌ |
| 跨进程 | CBS / WebSocket | ❌ | 部分引擎有 | NFS / SMB |

**关键差异**：ve 比 React/Zustand 多了"**树结构**"和"**子节点生命周期信号**"（added/removed/activated），这正是 JS 端目前缺失的能力。

### 2.3 信号传播机制

```
        robot                    watch=true
        ├── state                watch=true
        │   ├── power ← set(1)
        │   │   └── 发出 changed(1, 0, sender)
        │   │
        │   └── ← 收到 activated(power, CHANGE, sender)   [因为 watch=true]
        │
        └── ← 收到 activated(power, CHANGE, sender)       [因为 watch=true，继续冒泡]
```

- `changed` —— 仅当节点自身值变化时发出
- `activated` —— 子树中任意节点发生 CHANGE/INSERT/REMOVE/REORDER 时向上冒泡（需要 `watch=true`）
- `quiet` —— 抑制所有信号（批量更新时用）

这套机制等价于 DOM 的事件冒泡，但更轻量。

---

## 3. 当前架构的耦合问题

### 3.1 Qt 深度耦合

ve::Data（ModuleObject）直接继承 QObject，导致：

| 依赖项 | 用途 | 是否可替代 |
|--------|------|-----------|
| QObject | 信号槽、parent/child、生命周期 | ✅ 可用自定义 signal + raw ptr |
| QVariant | 万能值存储 | ⚠️ 替代品较弱（std::any, nlohmann::json, 模板） |
| QHash | 子节点存储 | ✅ std::unordered_map |
| QMutex | 线程安全 | ✅ std::mutex |
| QString | 字符串 | ✅ std::string |
| QJsonValue/QXmlStream/QDataStream | 序列化 | ✅ nlohmann::json / pugixml / 自定义 |

**最大难点是 QVariant 的替代**。QVariant 的能力：
- 存任意类型（基础类型 + Qt 类型 + 自定义类型）
- 运行时类型查询和转换
- 自动序列化（QDataStream 支持）

纯 C++ 替代方案：
- `std::any` —— 能存但不能查/转/序列化
- `std::variant<types...>` —— 编译期固定类型集合
- `nlohmann::json` —— JSON 语义的动态值，覆盖大多数场景
- 模板化 Data<T> —— 类型安全但失去统一接口

### 3.2 两套对象系统的分裂

```
ve::Object (纯C++)              imol::ModuleObject (Qt)
├── name()                      ├── name()
├── parent() / setParent()      ├── pmobj() / setPmobj()
├── connect(signal, observer)   ├── QObject::connect(signal, slot)
├── trigger(signal)             ├── emit changed(...)
└── Manager (HashMap容器)        └── 完整树操作(c/p/b/r/insert/remove...)
```

两者概念重叠但不兼容——`ve::Object` 有自己的 signal/connect/trigger，`ModuleObject` 用 Qt 的。`ve::Module` 继承 `ve::Object`，`ve::Data` 是 `ModuleObject` 的别名。这造成：

- 纯 C++ 模块用 `ve::Object` 的信号，Qt 模块用 `QObject` 的信号，两套不互通
- `ve::Manager` 是扁平 HashMap，`ModuleObject` 是完整树——两种容器语义不同
- 工厂、版本管理等基于 `ve::Object`，数据树基于 `ModuleObject`——概念分裂

### 3.3 各端适配散落在项目外

- **QML 适配**（QuickNode）在 MozHMI 中，不在 ve 仓库
- **JS 适配**（veservice.js + WebSocket Server）在 MozHMI 中
- **纯 C++ 适配**（模板 Data<T>）未纳入，且"不如 QVariant 好使"

每到新项目就需要"略微改动以适配"——说明适配层的抽象不够。

---

## 4. 演进目标

基于以上分析，ve 的演进目标应该是：

### 4.1 分层解耦

```
┌──────────────────────────────────────────────────────┐
│ 适配层 (Adapter)                                      │
│  ├── ve-qt      Qt/QML 适配（QObject 桥接、QVariant） │
│  ├── ve-js      JavaScript 适配（WebSocket/注入）     │
│  └── ve-pure    纯 C++ 适配（无 Qt 依赖）             │
├──────────────────────────────────────────────────────┤
│ 服务层 (Service)                                      │
│  ├── CBS        二进制 IPC                            │
│  ├── WebSocket  JSON 协议                             │
│  └── Command    文本命令                              │
├──────────────────────────────────────────────────────┤
│ 核心层 (Core) ← 零外部依赖，纯 C++17                  │
│  ├── Node       数据树节点（值 + 子节点 + 信号）       │
│  ├── Value      万能值类型（替代 QVariant）            │
│  ├── Module     模块生命周期                           │
│  ├── Path       路径寻址                              │
│  └── Signal     轻量信号系统                          │
└──────────────────────────────────────────────────────┘
```

### 4.2 核心目标

1. **Core 零依赖**：核心层仅依赖 C++17 STL，不依赖 Qt 或任何第三方库
2. **Value 足够强**：核心值类型覆盖 null, bool, int, double, string, bytes, array, object（类似 JSON + 二进制扩展）
3. **信号完整**：保留 ve 现有的全套信号语义（changed, activated, added, removed, gonnaInsert, gonnaRemove）
4. **适配器模式**：Qt/QML/JS 作为适配层，复用核心层，不修改核心代码
5. **项目无需改 ve**：通过配置和适配层，而非修改 ve 源码来适配项目

---

## 5. 重构方案

### 5.1 核心值类型 —— `ve::Value`

替代 QVariant，覆盖跨语言交换所需的类型集合：

```cpp
namespace ve {

class Value {
public:
    enum Type : uint8_t {
        Null, Bool, Int, Double, String, Bytes, Array, Object
    };

    Value();                              // Null
    Value(bool v);
    Value(int64_t v);
    Value(double v);
    Value(const std::string& v);
    Value(std::string&& v);
    Value(const std::vector<uint8_t>& v); // 二进制
    Value(std::vector<Value>&& v);        // 数组
    Value(std::unordered_map<std::string, Value>&& v); // 对象

    Type type() const;
    bool isNull() const;

    // 取值（类型不匹配返回默认值）
    bool        toBool(bool def = false) const;
    int64_t     toInt(int64_t def = 0) const;
    double      toDouble(double def = 0.0) const;
    std::string toString(const std::string& def = "") const;
    // ...

    // 比较
    bool operator==(const Value& other) const;
    bool operator!=(const Value& other) const;

private:
    Type _type;
    std::variant<
        std::monostate,                          // Null
        bool,                                    // Bool
        int64_t,                                 // Int
        double,                                  // Double
        std::string,                             // String
        std::vector<uint8_t>,                    // Bytes
        std::vector<Value>,                      // Array
        std::unordered_map<std::string, Value>   // Object
    > _data;
};

} // namespace ve
```

**设计取舍：**
- 不支持任意类型注册（QVariant 的 `Q_DECLARE_METATYPE` 能力）——这是有意为之，因为跨语言场景不需要
- 如果 Qt 适配层需要存 QVariant，可以在 ve-qt 适配层做双向转换
- Bytes 类型用于替代 QByteArray，支持点云等大块二进制数据的传输

### 5.2 核心节点 —— `ve::Node`

从 ModuleObject 提炼，去 Qt 依赖：

```cpp
namespace ve {

class Node {
public:
    // ActivateType 保持和原 imol 一致
    enum ActivateType { CHANGE, INSERT, REMOVE, REORDER };

    explicit Node(const std::string& name = "", Node* parent = nullptr);
    virtual ~Node();

    // --- 自身属性 ---
    const std::string& name() const;
    bool isNull() const; // EmptyNode 返回 true

    // --- 值操作 ---
    const Value& get() const;
    const Value& get(const Value& default_value) const;
    Node* set(const Value& value);
    Node* set(const std::string& rpath, const Value& value);
    void trigger(); // 不改值，仅发信号

    // --- 树导航（保留 imol 的完整语义）---
    Node* parent() const;              // p()
    Node* child(const std::string& rname) const; // c()
    Node* child(int index) const;
    Node* sibling(int offset) const;   // b()
    Node* relative(const std::string& rpath) const; // r()
    std::string fullPath(Node* ancestor = nullptr) const;

    // --- 子节点管理 ---
    int childCount() const;
    bool hasChild(const std::string& rname) const;
    std::vector<std::string> childNames() const;
    std::vector<Node*> children() const;

    Node* insert(Node* child, int index = -1);
    Node* append(const std::string& name = "");
    bool remove(Node* child, bool auto_delete = true);
    bool remove(const std::string& rpath, bool auto_delete = true);

    // --- 信号 ---
    // changed: 值变化
    using ChangedCallback = std::function<void(const Value& new_val, const Value& old_val)>;
    int onChanged(ChangedCallback cb);
    void offChanged(int id);

    // activated: 子树变化冒泡（需 watch=true）
    using ActivatedCallback = std::function<void(Node* changed_node, ActivateType type)>;
    int onActivated(ActivatedCallback cb);
    void offActivated(int id);

    // added/removed: 直接子节点增删
    using ChildCallback = std::function<void(const std::string& rname)>;
    int onAdded(ChildCallback cb);
    int onRemoved(ChildCallback cb);

    // --- 控制 ---
    bool isQuiet() const;
    void setQuiet(bool quiet, bool recursive = false);
    bool isWatching() const;
    void setWatching(bool watching, bool recursive = false);

    // --- 序列化 ---
    // 核心层只做 Value 级别的序列化；JSON/XML/Binary 放在工具层或适配层
    Value exportToValue() const;  // 子树 → 嵌套 Value
    void importFromValue(const Value& v);  // 嵌套 Value → 子树

private:
    struct Private;
    std::unique_ptr<Private> _p;
};

// 全局访问器（保留 ve::d 的便捷性）
Node* d(const std::string& path);
Node* d(Node* root, const std::string& path);

} // namespace ve
```

### 5.3 与现有 `ve::Object` 的关系

现有 `ve::Object` 保留，作为模块系统的基类（它已经是纯 C++ 的）。`ve::Node` 是新的核心数据节点，不继承 `ve::Object`——它们职责不同：

- `ve::Object` —— 通用对象基类，有名字、父子关系、整数信号
- `ve::Node` —— 数据树节点，有值、路径导航、数据变化信号、序列化
- `ve::Module` —— 继续继承 `ve::Object`，管理生命周期

### 5.4 模块系统保持不变

`ve::Module` 的 NONE→INIT→READY→DEINIT 生命周期、`VE_REGISTER_MODULE` 宏、`globalModuleFactory()` 这些都不需要改——它们已经是纯 C++ 的（只是 Module 构造函数里用了 `data::at` 获取名字，这个细节可以调整）。

---

## 6. 各端适配层设计

### 6.1 ve-qt：Qt/QML 适配

```cpp
namespace ve::qt {

// QVariant ↔ Value 双向转换
Value fromQVariant(const QVariant& var);
QVariant toQVariant(const Value& val);

// Qt 桥接节点——包装 ve::Node，暴露 Qt 信号
class QNode : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariant value READ value WRITE setValue NOTIFY valueChanged)

public:
    explicit QNode(ve::Node* node, QObject* parent = nullptr);

    QVariant value() const;
    void setValue(const QVariant& var);

signals:
    void valueChanged(const QVariant& value);
    void childAdded(const QString& name);
    void childRemoved(const QString& name);
    void activated(const QString& changedPath, int type);

private:
    ve::Node* _node;
};

// QuickNode (QML 用) 从 QNode 继承，注册为 QML 类型
// 这层代码从 MozHMI 移入 ve-qt

} // namespace ve::qt
```

**好处**：
- MozHMI 中的 QuickNode 直接移入 ve 仓库，不再散落
- ve::Node 和 QObject 信号自动桥接，不需要手写 connect
- QVariant ↔ Value 转换只在这一层发生

### 6.2 ve-js：JavaScript 适配

现有架构中 JS 端通过 WebSocket 与后端通信，`veservice.js` 注入到 WebView。这套协议可以保留，但在 JS 端增加**树结构镜像**：

```typescript
// ve-js: 轻量级 JS 版 Node（不需要完全复刻，只做代理）

class VeNode {
    readonly path: string;
    private _value: any;
    private _children: Map<string, VeNode>;
    private _listeners: Map<string, Set<Function>>;
    private _parent: VeNode | null;

    constructor(path: string, parent?: VeNode);

    // 子节点访问（惰性创建代理）
    child(name: string): VeNode;

    // 值操作（自动同步到后端）
    get value(): any;
    set value(v: any);

    // 信号
    onChange(callback: (newVal: any, oldVal: any) => void): () => void;
    onChildAdded(callback: (name: string) => void): () => void;
    onChildRemoved(callback: (name: string) => void): () => void;

    // React hook
    useValue<T = any>(): T;
    useChildren(): VeNode[];

    // 与 veService 的桥接
    subscribe(): void;   // 订阅后端数据
    unsubscribe(): void;
    publish(): void;      // 推送到后端
}

// 全局根节点
const veTree = new VeNode("");

// 使用示例
const leftArm = veTree.child("movax").child("robot").child("mu").child("LeftArm");
const power = leftArm.child("state").child("power");

// React 组件中
function LeftArmPanel() {
    const powerVal = power.useValue<number>();
    const joints = leftArm.child("value").child("joints").useValue<number[]>();
    // ...
}
```

**替代当前 8 个扁平 Zustand store**，一棵 VeNode 树就够了：

```
当前:                              VeNode 方案:
useRobotConfigStore     →    veTree.child("movax.robot.global.config")
useRobotStateStore      →    veTree.child("movax.robot.mu.{name}.state")
useRobotValueStore      →    veTree.child("movax.robot.mu.{name}.value")
useRobotViewStore       →    veTree.child("movax.robot.visual")
useTeleopStore          →    veTree.child("movax.channel.grpc.teleop")
useWorkpointStore       →    veTree.child("movax.channel.grpc.workpoint")
...
```

App.jsx 中几十行手动 subscribe 代码 → 消失，因为每个组件自己声明关心哪个节点。

### 6.3 ve-pure：纯 C++ 适配

纯 C++ 后端直接使用 `ve::Node` + `ve::Value`，不需要额外适配层。`ve::Value` 就是 QVariant 的纯 C++ 替代品。

对于已有项目中用到 QVariant 特有能力（如 QPointF、QSize 等 Qt 类型）的场景，通过 ve-qt 适配层的 `fromQVariant()` / `toQVariant()` 来做转换。

---

## 7. 从 Tree 到 Mesh

### 7.1 当前局限——纯树

现在每个节点只有一个 parent，没有交叉引用。但实际场景中常需要：

- LeftArm 的 collision.level 和 global.safety 关联
- 某个 workpoint 引用多个 MU 的 joint 值
- UI 组件同时依赖多个不相关子树的数据

### 7.2 Mesh 扩展思路

在保持树为主结构的基础上，增加**链接（Link）**机制：

```cpp
class Node {
    // ... 现有树操作 ...

    // Link: 引用另一个节点（不改变树结构）
    void link(const std::string& alias, Node* target);
    void link(const std::string& alias, const std::string& target_path);
    Node* linked(const std::string& alias) const;

    // 当 target 节点值变化时，linked 节点也收到通知
    // 实现上：link 内部自动 subscribe target 的 changed 信号
};
```

这让树变成了 **DAG**（有向无环图），但主结构仍然是树（parent/child 关系不变），link 只是额外的引用关系。

**使用示例：**

```cpp
// 左臂碰撞等级链接到全局安全状态
ve::d("movax.robot.mu.LeftArm.state.collision")->link("safety", "movax.robot.global.state.safety");

// JS 端也可以
leftArm.child("state.collision").link("safety", veTree.child("movax.robot.global.state.safety"));
```

### 7.3 进一步——Computed Node

类似 Vue 的 computed，一个节点的值由其他节点计算得出：

```cpp
// 伪代码
ve::d("robot.status.summary")->compute([](Node* self) {
    auto power = ve::d("robot.global.state.power")->get().toInt();
    auto safety = ve::d("robot.global.state.safety")->get().toInt();
    return Value(power > 0 && safety == 0 ? "ready" : "not_ready");
}, {"robot.global.state.power", "robot.global.state.safety"}); // 依赖列表
```

这个可以放到后续版本，先把核心的 Tree + Link 做稳。

---

## 8. 实施路线

### Phase 0：整理（当前可做）

- [x] 总结现有架构（本文档）
- [ ] 将 MozHMI 中的 WebSocket Server、QuickNode、veservice.js 代码梳理，明确哪些应该移入 ve
- [ ] 确定 ve::Value 的类型集合（是否需要扩展 Bytes 之外的类型）

### Phase 1：Core 层去 Qt 依赖

- [ ] 实现 `ve::Value`（纯 C++17，替代 QVariant 在核心层的角色）
- [ ] 实现 `ve::Node`（从 ModuleObject 提炼，保留完整的树操作和信号语义）
- [ ] 实现核心层的序列化（Value ↔ JSON string，可选 Binary）
- [ ] 保留 `ve::d("path")` 全局访问器
- [ ] 保留 `ve::Module` 不变（它已经主要依赖 `ve::Object`）
- [ ] 单元测试覆盖核心层

### Phase 2：适配层

- [ ] **ve-qt**：实现 QNode 桥接、QVariant ↔ Value 转换
- [ ] **ve-qt**：将 QuickNode 从 MozHMI 移入，基于 QNode 重构
- [ ] **ve-js**：实现 VeNode（JS 树代理），与 veservice.js 协议对接
- [ ] **ve-js**：提供 React hooks（useVeValue, useVeChildren）
- [ ] CBS/WebSocket/Command Server 保持基本不变，协议层稳定

### Phase 3：Mesh 扩展

- [ ] Node.link() 引用机制
- [ ] 跨节点信号传播
- [ ] Computed Node（可选）

### Phase 4：JS 端实战验证

- [ ] 在 moz_hmi 项目中用 VeNode 替换现有 Zustand stores
- [ ] 消除 App.jsx 中的手动 subscribe 代码
- [ ] 验证性能（大量实时关节数据流）

---

## 附录：核心设计决策记录

### 为什么不用 MobX-State-Tree / Valtio 等现成方案？

因为 ve 的场景是"跨语言数据树镜像"，而非"JS 端状态管理"。现有 JS 方案解决的是"JS 内部的响应式状态"，但 ve 要解决的是"C++ 数据树 ↔ JS 数据树 的双向实时同步"，必须和 ve 协议对齐。

### 为什么 Value 不用 std::any？

`std::any` 只能存和取，不能比较、不能序列化、不能跨语言。ve::Value 需要做到：
1. 值比较（判断是否真的变化了，避免无意义的信号）
2. 序列化（CBS 二进制传输、WebSocket JSON 传输）
3. 跨语言映射（C++ Value ↔ JSON ↔ QVariant ↔ JS any）

### 为什么保留 ve::Object 而不合并到 Node？

`ve::Object` 是通用对象基类（模块、工厂都用），`ve::Node` 专注数据树。保持分离，职责清晰。如果合并，Node 会承载过多不相关的功能。

---

*文档版本: 1.0.0*
*更新日期: 2026-02-26*
*Author: Thilo*
