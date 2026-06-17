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

    if (!runtime_node) return true;

    runtime_node->set("state", Var("ready"));
    runtime_node->set("backend_active", Var(current->key()));

    backendInfoList(runtime_node->at("backends"));
    envInfo(runtime_node->at("env"));
    schema::VarS::toNode(runtime_node->at("nodes"), Var(current->listNodes()), Node::COPY_STRICT);
    schema::VarS::toNode(runtime_node->at("topics"), Var(current->listTopics()), Node::COPY_STRICT);
    schema::VarS::toNode(runtime_node->at("services"), Var(current->listServices()), Node::COPY_STRICT);
    return true;
}

Result runtimeInfo(Node* out)
{
    auto current = activeBackend();
    if (!current) return Result::fail("no active ROS backend");
    out->set("backend_active", Var(current->key()));
    current->info(out->at("backend"));
    backendInfoList(out->at("backends"));
    envInfo(out->at("env"));
    schema::VarS::toNode(out->at("nodes"), Var(current->listNodes()), Node::COPY_STRICT);
    schema::VarS::toNode(out->at("topics"), Var(current->listTopics()), Node::COPY_STRICT);
    schema::VarS::toNode(out->at("services"), Var(current->listServices()), Node::COPY_STRICT);
    return Result::ok();
}

Var::ListV listNodes(const std::string& filter)
{
    if (auto current = activeBackend())
        return current->listNodes(filter);
    return {};
}

Result listParams(const std::string& node_name, Node* out)
{
    auto current = activeBackend();
    if (!current) return Result::fail("no active ROS backend");
    return current->listParams(node_name, out);
}

Result getParam(const std::string& node_name, const std::string& name, Node* out)
{
    auto current = activeBackend();
    if (!current) return Result::fail("no active ROS backend");
    return current->getParam(node_name, name, out);
}

Result setParam(const std::string& node_name, const std::string& name, const Var& value, Node* out)
{
    auto current = activeBackend();
    if (!current) return Result::fail("no active ROS backend");
    return current->setParam(node_name, name, value, out);
}

} // namespace ve::ros
