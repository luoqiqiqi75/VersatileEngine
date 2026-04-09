#include "ve/ros/backend.h"

#include <cctype>
#include <cstdlib>
#include <mutex>

namespace ve::ros {

namespace {

struct Registry {
    std::mutex mu;
    Dict<BackendPtr> items;
    Strings order;
};

Registry& registry()
{
    static Registry r;
    return r;
}

std::string lowerCopy(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

void insertBackendLocked(const BackendPtr& backend_ptr)
{
    if (!backend_ptr || backend_ptr->key().empty())
        return;

    auto& r = registry();
    if (!r.items.has(backend_ptr->key()))
        r.order.push_back(backend_ptr->key());
    r.items.insertOne(backend_ptr->key(), backend_ptr);
}

class PlaceholderFastDdsBackend : public Backend
{
public:
    std::string key() const override { return "fastdds"; }
    std::string displayName() const override { return "Fast DDS"; }
    std::string transport() const override { return "native-dds"; }
    int priority() const override { return 80; }
    std::string summary() const override
    {
        return "Fast DDS backend is compiled separately and registered when available.";
    }

    bool isAvailable() const override { return false; }
    bool start(Node*, std::string& error) override
    {
        error = "Fast DDS backend is not available in this build";
        return false;
    }
    void stop() override {}
    bool isStarted() const override { return false; }

    Var::ListV listNodes(const std::string&) const override { return {}; }
    Var::ListV listTopics(const std::string&) const override { return {}; }
    Var::DictV topicInfo(const std::string&) const override { return {}; }
    Var::DictV subscribeTopic(const TopicSubscriptionConfig& config) override
    {
        Var::DictV result;
        result["ok"] = Var(false);
        result["message"] = Var("Fast DDS placeholder backend does not implement topic subscribe");
        result["name"] = Var(config.name);
        result["topic"] = Var(config.topic);
        return result;
    }
    Var::DictV unsubscribeTopic(const std::string& name) override
    {
        Var::DictV result;
        result["ok"] = Var(false);
        result["message"] = Var("Fast DDS placeholder backend does not implement topic unsubscribe");
        result["name"] = Var(name);
        return result;
    }
    Var::DictV publishTopic(const TopicPublishRequest& request) override
    {
        Var::DictV result;
        result["ok"] = Var(false);
        result["message"] = Var("Fast DDS placeholder backend does not implement topic publish");
        result["topic"] = Var(request.topic);
        return result;
    }
    Var::DictV onceTopic(const TopicOnceRequest& request) override
    {
        Var::DictV result;
        result["ok"] = Var(false);
        result["message"] = Var("Fast DDS placeholder backend does not implement topic once");
        result["topic"] = Var(request.topic);
        return result;
    }
    Var::ListV listServices(const std::string&) const override { return {}; }
    Var::DictV serviceInfo(const std::string&) const override { return {}; }
    Var::DictV listParams(const std::string&) const override { return {}; }
    Var::DictV getParam(const std::string&, const std::string&) const override { return {}; }
    Var::DictV setParam(const std::string&, const std::string&, const Var&) const override { return {}; }
};

void registerBuiltins()
{
    static const bool once = []() {
        std::lock_guard<std::mutex> lock(registry().mu);
        insertBackendLocked(std::make_shared<PlaceholderFastDdsBackend>());
        return true;
    }();
    (void)once;
}

} // namespace

Backend::~Backend() = default;

Var::DictV Backend::info() const
{
    Var::DictV dict;
    dict["key"] = Var(key());
    dict["display_name"] = Var(displayName());
    dict["transport"] = Var(transport());
    dict["available"] = Var(isAvailable());
    dict["enabled"] = Var(isEnabled());
    dict["started"] = Var(isStarted());
    dict["priority"] = Var(static_cast<int64_t>(priority()));
    dict["summary"] = Var(summary());
    dict["details"] = Var(details());
    return dict;
}

void registerBackend(BackendPtr backend_ptr)
{
    registerBuiltins();
    std::lock_guard<std::mutex> lock(registry().mu);
    insertBackendLocked(backend_ptr);
}

bool hasBackend(const std::string& key)
{
    registerBuiltins();
    std::lock_guard<std::mutex> lock(registry().mu);
    return registry().items.has(key);
}

BackendPtr backend(const std::string& key)
{
    registerBuiltins();
    std::lock_guard<std::mutex> lock(registry().mu);
    return registry().items.value(key, BackendPtr{});
}

BackendPtr defaultBackend()
{
    registerBuiltins();

    const std::string requested = env("VE_ROS_BACKEND");
    if (!requested.empty()) {
        if (auto b = backend(requested))
            return b;
    }

    std::lock_guard<std::mutex> lock(registry().mu);
    BackendPtr best;
    for (const auto& key : registry().order) {
        auto current = registry().items.value(key, BackendPtr{});
        if (!current || !current->isAvailable())
            continue;
        if (!best || current->priority() < best->priority())
            best = current;
        if (current->isEnabled())
            return current;
    }
    return best;
}

Var::ListV backendInfoList()
{
    registerBuiltins();

    Var::ListV list;
    std::lock_guard<std::mutex> lock(registry().mu);
    for (const auto& key : registry().order) {
        auto current = registry().items.value(key, BackendPtr{});
        if (current)
            list.push_back(Var(current->info()));
    }
    return list;
}

Strings backendKeys()
{
    registerBuiltins();
    std::lock_guard<std::mutex> lock(registry().mu);
    return registry().order;
}

bool isBackendStarted(const std::string& key)
{
    if (auto b = backend(key))
        return b->isStarted();
    return false;
}

std::string env(const std::string& name, const std::string& def)
{
    if (name.empty())
        return def;
    const char* value = std::getenv(name.c_str());
    return value ? std::string(value) : def;
}

Var::DictV envInfo()
{
    Var::DictV dict;
    dict["ROS_DOMAIN_ID"] = Var(env("ROS_DOMAIN_ID", "0"));
    dict["RMW_IMPLEMENTATION"] = Var(env("RMW_IMPLEMENTATION"));
    dict["FASTRTPS_DEFAULT_PROFILES_FILE"] = Var(env("FASTRTPS_DEFAULT_PROFILES_FILE"));
    dict["CYCLONEDDS_URI"] = Var(env("CYCLONEDDS_URI"));
    dict["ROS_AUTOMATIC_DISCOVERY_RANGE"] = Var(env("ROS_AUTOMATIC_DISCOVERY_RANGE"));
    dict["VE_ROS_BACKEND"] = Var(env("VE_ROS_BACKEND"));
    return dict;
}

} // namespace ve::ros
