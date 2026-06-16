#include "ve/ros/backend.h"

#include "ve/core/schema.h"

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
    Result topicInfo(const std::string&, Node*) const override { return Result::fail("placeholder backend"); }
    Result subscribeTopic(const TopicSubscriptionConfig&, Node*) override { return Result::fail("placeholder backend"); }
    Result unsubscribeTopic(const std::string&, Node*) override { return Result::fail("placeholder backend"); }
    Result publishTopic(const TopicPublishRequest&, Node*) override { return Result::fail("placeholder backend"); }
    Result onceTopic(const TopicOnceRequest&, Node*) override { return Result::fail("placeholder backend"); }
    Var::ListV listServices(const std::string&) const override { return {}; }
    Result serviceInfo(const std::string&, Node*) const override { return Result::fail("placeholder backend"); }
    Result callService(const ServiceCallRequest&, Node*) override { return Result::fail("placeholder backend"); }
    Result listParams(const std::string&, Node*) const override { return Result::fail("placeholder backend"); }
    Result getParam(const std::string&, const std::string&, Node*) const override { return Result::fail("placeholder backend"); }
    Result setParam(const std::string&, const std::string&, const Var&, Node*) const override { return Result::fail("placeholder backend"); }
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

void Backend::info(Node* out) const
{
    out->set("key", Var(key()));
    out->set("display_name", Var(displayName()));
    out->set("transport", Var(transport()));
    out->set("available", Var(isAvailable()));
    out->set("enabled", Var(isEnabled()));
    out->set("started", Var(isStarted()));
    out->set("priority", Var(static_cast<int64_t>(priority())));
    out->set("summary", Var(summary()));
    schema::VarS::importNode(out->at("details"), Var(details()), Node::COPY_STRICT);
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

void backendInfoList(Node* out)
{
    registerBuiltins();
    std::lock_guard<std::mutex> lock(registry().mu);
    for (const auto& key : registry().order) {
        auto current = registry().items.value(key, BackendPtr{});
        if (current)
            current->info(out->at(key));
    }
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

void envInfo(Node* out)
{
    out->set("ROS_DOMAIN_ID", Var(env("ROS_DOMAIN_ID", "0")));
    out->set("RMW_IMPLEMENTATION", Var(env("RMW_IMPLEMENTATION")));
    out->set("FASTRTPS_DEFAULT_PROFILES_FILE", Var(env("FASTRTPS_DEFAULT_PROFILES_FILE")));
    out->set("CYCLONEDDS_URI", Var(env("CYCLONEDDS_URI")));
    out->set("ROS_AUTOMATIC_DISCOVERY_RANGE", Var(env("ROS_AUTOMATIC_DISCOVERY_RANGE")));
    out->set("VE_ROS_BACKEND", Var(env("VE_ROS_BACKEND")));
}

} // namespace ve::ros
