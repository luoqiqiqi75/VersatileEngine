#include "ve/ros/runtime.h"

#include "ve/ros/service.h"
#include "ve/ros/topic.h"
#include "ve/core/schema.h"

#include <mutex>

namespace ve::ros {

namespace {

struct RuntimeState {
    std::mutex mu;
    BackendPtr active;
    Node* runtime_node = nullptr;
};

RuntimeState& runtimeState()
{
    static RuntimeState state;
    return state;
}

void writeRuntimeValue(Node* runtime_node, const std::string& key, const Var& value)
{
    if (runtime_node)
        runtime_node->set(key, value);
}

void writeRuntimeTree(Node* runtime_node, const std::string& key, const Var& value)
{
    if (!runtime_node)
        return;

    auto* target = runtime_node->at(key);
    target->clear();
    target->set(Var());
    schema::importAs<schema::VarS>(target, value);
}

Var::DictV makeResult(bool ok, std::string message)
{
    Var::DictV result;
    result["ok"] = Var(ok);
    result["message"] = Var(std::move(message));
    return result;
}

} // namespace

bool activateBackend(const std::string& requested_key,
                     Node* runtime_node,
                     std::string& error)
{
    BackendPtr next = requested_key.empty() ? defaultBackend() : backend(requested_key);
    if (!next) {
        error = requested_key.empty()
            ? "no ROS backend is available"
            : ("ROS backend not found: " + requested_key);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(runtimeState().mu);
        if (runtimeState().active && runtimeState().active->key() != next->key())
            runtimeState().active->stop();
        runtimeState().runtime_node = runtime_node;
    }

    if (!next->start(runtime_node, error))
        return false;

    {
        std::lock_guard<std::mutex> lock(runtimeState().mu);
        runtimeState().active = next;
    }
    return refreshRuntime(runtime_node, error);
}

void deactivateBackend()
{
    std::lock_guard<std::mutex> lock(runtimeState().mu);
    if (runtimeState().active)
        runtimeState().active->stop();
    runtimeState().active.reset();
    runtimeState().runtime_node = nullptr;
}

BackendPtr activeBackend()
{
    std::lock_guard<std::mutex> lock(runtimeState().mu);
    return runtimeState().active;
}

std::string activeBackendKey()
{
    if (auto current = activeBackend())
        return current->key();
    return "";
}

bool refreshRuntime(Node* runtime_node, std::string& error)
{
    BackendPtr current;
    {
        std::lock_guard<std::mutex> lock(runtimeState().mu);
        if (runtime_node)
            runtimeState().runtime_node = runtime_node;
        current = runtimeState().active;
        runtime_node = runtimeState().runtime_node;
    }

    if (!current) {
        error = "no active ROS backend";
        return false;
    }

    writeRuntimeValue(runtime_node, "state", Var("ready"));
    writeRuntimeValue(runtime_node, "backend_active", Var(current->key()));
    writeRuntimeTree(runtime_node, "backends", Var(backendInfoList()));
    writeRuntimeTree(runtime_node, "env", Var(envInfo()));
    writeRuntimeTree(runtime_node, "nodes", Var(current->listNodes()));
    writeRuntimeTree(runtime_node, "topics", Var(current->listTopics()));
    writeRuntimeTree(runtime_node, "services", Var(current->listServices()));
    writeRuntimeTree(runtime_node, "params", Var(Var::DictV{}));
    return true;
}

Var::DictV runtimeInfo()
{
    Var::DictV info = makeResult(false, "no active ROS backend");
    if (auto current = activeBackend()) {
        info["ok"] = Var(true);
        info["message"] = Var("ros runtime ready");
        info["backend_active"] = Var(current->key());
        info["backend"] = Var(current->info());
        info["backends"] = Var(backendInfoList());
        info["env"] = Var(envInfo());
        info["nodes"] = Var(current->listNodes());
        info["topics"] = Var(current->listTopics());
        info["services"] = Var(current->listServices());
    }
    return info;
}

Var::ListV listNodes(const std::string& filter)
{
    if (auto current = activeBackend())
        return current->listNodes(filter);
    return {};
}

Var::DictV listParams(const std::string& node_name)
{
    if (auto current = activeBackend())
        return current->listParams(node_name);
    return makeResult(false, "no active ROS backend");
}

Var::DictV getParam(const std::string& node_name, const std::string& name)
{
    if (auto current = activeBackend())
        return current->getParam(node_name, name);
    return makeResult(false, "no active ROS backend");
}

Var::DictV setParam(const std::string& node_name, const std::string& name, const Var& value)
{
    if (auto current = activeBackend())
        return current->setParam(node_name, name, value);
    return makeResult(false, "no active ROS backend");
}

} // namespace ve::ros
