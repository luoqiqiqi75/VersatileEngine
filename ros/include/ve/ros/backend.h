#pragma once

#include "ve/core/node.h"
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
    virtual Var::DictV topicInfo(const std::string& topic) const = 0;
    virtual Var::DictV subscribeTopic(const TopicSubscriptionConfig& config) = 0;
    virtual Var::DictV unsubscribeTopic(const std::string& name) = 0;
    virtual Var::DictV publishTopic(const TopicPublishRequest& request) = 0;
    virtual Var::DictV onceTopic(const TopicOnceRequest& request) = 0;
    virtual Var::ListV listServices(const std::string& filter = "") const = 0;
    virtual Var::DictV serviceInfo(const std::string& service) const = 0;
    virtual Var::DictV listParams(const std::string& node_name = "") const = 0;
    virtual Var::DictV getParam(const std::string& node_name, const std::string& name) const = 0;
    virtual Var::DictV setParam(const std::string& node_name, const std::string& name, const Var& value) const = 0;

    Var::DictV info() const;
};

using BackendPtr = std::shared_ptr<Backend>;

VE_API void registerBackend(BackendPtr backend);
VE_API bool hasBackend(const std::string& key);
VE_API BackendPtr backend(const std::string& key);
VE_API BackendPtr defaultBackend();
VE_API Var::ListV backendInfoList();
VE_API Strings backendKeys();
VE_API bool isBackendStarted(const std::string& key);

VE_API std::string env(const std::string& name, const std::string& def = "");
VE_API Var::DictV envInfo();

} // namespace ve::ros
