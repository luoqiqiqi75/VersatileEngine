#pragma once

#include "ve/core/node.h"
#include "ve/ros/service.h"
#include "ve/ros/topic.h"

namespace ve::ros {

class VE_API Backend
{
public:
    virtual ~Backend();

    virtual std::string key() const = 0;
    virtual std::string displayName() const { return key(); }
    virtual std::string transport() const { return ""; }
    virtual bool isAvailable() const { return true; }
    virtual bool isEnabled() const { return false; }
    virtual int priority() const { return 100; }
    virtual std::string summary() const { return ""; }
    virtual Var::DictV details() const { return {}; }
    virtual bool start(Node* runtime_node, std::string& error) = 0;
    virtual void stop() = 0;
    virtual bool isStarted() const = 0;

    virtual Var::ListV listNodes(const std::string& filter = "") const = 0;
    virtual Var::ListV listTopics(const std::string& filter = "") const = 0;
    virtual Result topicInfo(const std::string& topic, Node* out) const = 0;
    virtual Result subscribeTopic(const TopicSubscriptionConfig& config, Node* out) = 0;
    virtual Result unsubscribeTopic(const std::string& name, Node* out) = 0;
    virtual Result publishTopic(const TopicPublishRequest& request, Node* out) = 0;
    virtual Result onceTopic(const TopicOnceRequest& request, Node* out) = 0;
    virtual Var::ListV listServices(const std::string& filter = "") const = 0;
    virtual Result serviceInfo(const std::string& service, Node* out) const = 0;
    virtual Result callService(const ServiceCallRequest& request, Node* out) = 0;
    virtual Result listParams(const std::string& node_name, Node* out) const = 0;
    virtual Result getParam(const std::string& node_name, const std::string& name, Node* out) const = 0;
    virtual Result setParam(const std::string& node_name, const std::string& name, const Var& value, Node* out) const = 0;

    void info(Node* out) const;
};

using BackendPtr = std::shared_ptr<Backend>;

VE_API void registerBackend(BackendPtr backend);
VE_API bool hasBackend(const std::string& key);
VE_API BackendPtr backend(const std::string& key);
VE_API BackendPtr defaultBackend();
VE_API void backendInfoList(Node* out);
VE_API Strings backendKeys();
VE_API bool isBackendStarted(const std::string& key);

VE_API std::string env(const std::string& name, const std::string& def = "");
VE_API void envInfo(Node* out);

} // namespace ve::ros
