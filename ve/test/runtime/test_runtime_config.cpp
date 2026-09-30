#include "ve_test.h"
#include "ve/entry.h"
#include "ve/core/log.h"
#include "ve/core/loop.h"

#include <filesystem>
#include <fstream>

using namespace ve;
namespace fs = std::filesystem;

namespace {
struct TestDirectory
{
    fs::path path = fs::temp_directory_path() / ("ve_runtime_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    TestDirectory() { fs::create_directories(path); }
    ~TestDirectory() { std::error_code ec; fs::remove_all(path, ec); }
};
TestDirectory& directory() { static TestDirectory d; return d; }

bool setupJson(const std::string& json, Node* overrides = nullptr)
{
    node::root()->erase("ve/entry");
    auto path = directory().path / "runtime.json";
    { std::ofstream file(path); file << json; }
    Node options;
    options.at("blacklist")->append("")->set("ve");
    options.at("log/console/enabled")->set(false);
    static int sequence = 0;
    options.at("log/dir")->set((directory().path / ("config_" + std::to_string(++sequence))).string());
    options.copy(overrides);
    options.at("config_file")->set(path.string());
    if (!entry::setup(&options)) return false;
    entry::init();
    return true;
}

static bool _disable_logging()
{
    Node config;
    config.set("log/console/enabled", false);
    config.set("log/file/enabled", false);
    return log::configure(&config);
}

std::vector<std::string> lines(const std::string& path)
{
    std::ifstream file(path);
    std::vector<std::string> result;
    for (std::string line; std::getline(file, line);) { result.push_back(line); }
    return result;
}

struct CountFormatting
{
    int* count;
};
std::ostream& operator<<(std::ostream& stream, CountFormatting value)
{
    ++*value.count;
    return stream << "formatted";
}
} // namespace

VE_TEST(runtime_json_configures_async_logging) {
    auto dir = directory().path / "configured";
    // The path is installed through the caller overlay; other settings come
    // from an actual JSON file, exercising the same path as ve -c file.json.
    Node overrides;
    overrides.at("log/dir")->set(dir.string());
    VE_ASSERT(setupJson(R"({
        "log": {"level": "warn", "async": true, "queue_size": 32,
                "worker_threads": 2, "overflow_policy": "block",
                "flush_interval_seconds": 0, "flush_level": "off",
                "console": {"enabled": false},
                "file": {"enabled": true, "level": "info", "pattern": "%v"}}
    })", &overrides));
    auto path = log::getLogFilePath();
    VE_ASSERT(fs::path(path).parent_path() == dir);

    std::vector<std::thread> producers;
    for (int p = 0; p < 4; ++p) {
        producers.emplace_back([p] {
            for (int i = 0; i < 1000; ++i) { veLogI << p << ':' << i; }
        });
    }
    for (auto& producer : producers) { producer.join(); }
    veLogI << "last message";
    entry::deinit();
    VE_ASSERT_EQ(log::getLogFilePath(), path);
    veLogI << "after module shutdown";
    VE_ASSERT(_disable_logging());
    auto output = lines(path);
    VE_ASSERT_EQ(output.size(), size_t(4002));
    std::set<std::string> unique(output.begin(), output.end());
    VE_ASSERT_EQ(unique.size(), size_t(4002));
    VE_ASSERT(unique.count("last message") == 1);
    VE_ASSERT(unique.count("after module shutdown") == 1);
}

VE_TEST(runtime_pool_uses_compile_time_default_and_accepts_user_replacement) {
#ifdef VE_LOOP_POOL_THREADS
    const unsigned expected = VE_LOOP_POOL_THREADS;
#else
    const unsigned expected = std::max(1u, std::thread::hardware_concurrency()) * 2;
#endif
#ifdef __linux__
    auto threadCount = [] { return std::distance(fs::directory_iterator("/proc/self/task"), fs::directory_iterator{}); };
    auto before = threadCount();
#endif
    auto* pool = loop::pool();
#ifdef __linux__
    VE_ASSERT_EQ(threadCount() - before, expected);
#endif
    std::mutex mutex;
    std::condition_variable cv;
    bool release = false;
    std::set<std::thread::id> workers;
    std::atomic<int> done{0};
    for (unsigned i = 0; i < expected; ++i) {
        pool->post([&] {
            std::unique_lock<std::mutex> lock(mutex);
            workers.insert(std::this_thread::get_id());
            cv.notify_all();
            cv.wait(lock, [&] { return release; });
            ++done;
        });
    }
    {
        std::unique_lock<std::mutex> lock(mutex);
        VE_ASSERT(cv.wait_for(lock, std::chrono::seconds(2), [&] { return workers.size() == expected; }));
        release = true;
    }
    cv.notify_all();
    VE_ASSERT(VE_WAIT([&] { return done == static_cast<int>(expected); }));
    VE_ASSERT(setupJson("{}"));
    VE_ASSERT(loop::pool() == pool);
    AsioPoolLoop custom("custom", 2);
    loop::setPool(&custom);
    VE_ASSERT(loop::pool() == &custom);
    loop::setPool(nullptr);
    VE_ASSERT(loop::pool() == pool);
}

VE_TEST(runtime_rejects_invalid_json_settings) {
    VE_ASSERT(setupJson(R"({"log":{"async":true,"flush_interval_seconds":0,"file":{"pattern":"%v"}}})"));
    auto previous = log::getLogFilePath();
    for (const auto& json : {
        R"({"log":{"queue_size":0}})",
        R"({"log":{"queue_size":-2}})",
        R"({"log":{"worker_threads":1001}})",
        R"({"log":{"worker_threads":0}})",
        R"({"log":{"flush_interval_seconds":-1}})",
        R"({"log":{"overflow_policy":"discard"}})",
        R"({"log":{"overflow_policy":false}})",
        R"({"log":{"level":"unknown"}})"
    }) {
        VE_ASSERT(!setupJson(json));
        VE_ASSERT_EQ(log::getLogFilePath(), previous);
    }
    veLogI << "previous configuration retained";
    entry::deinit();
    VE_ASSERT(_disable_logging());
    auto output = lines(previous);
    VE_ASSERT_EQ(output.size(), size_t(1));
    if (!output.empty()) VE_ASSERT_EQ(output.front(), std::string("previous configuration retained"));
}

VE_TEST(runtime_log_settings_use_var_conversions) {
    VE_ASSERT(setupJson(R"({"log":{"async":"true","queue_size":"32",
        "worker_threads":"2","flush_interval_seconds":"0","flush_level":"off",
        "file":{"pattern":42}}})"));
    veLogD << "filtered";
    veLogI << "converted pattern";
    auto path = log::getLogFilePath();
    entry::deinit();
    VE_ASSERT(_disable_logging());
    auto output = lines(path);
    VE_ASSERT_EQ(output.size(), size_t(1));
    if (!output.empty()) VE_ASSERT_EQ(output.front(), std::string("42"));
}

VE_TEST(runtime_log_directory_error_returns_failure_and_keeps_previous_logger) {
    VE_ASSERT(setupJson(R"({"log":{"flush_interval_seconds":0,"file":{"pattern":"%v"}}})"));
    auto previous = log::getLogFilePath();
    auto blocker = directory().path / "regular_file";
    { std::ofstream file(blocker); file << "not a directory"; }
    Node overrides;
    overrides.at("log/dir")->set(blocker.string());
    VE_ASSERT(!setupJson("{}", &overrides));
    VE_ASSERT_EQ(log::getLogFilePath(), previous);
    veLogI << "still usable";
    entry::deinit();
    VE_ASSERT(_disable_logging());
    auto output = lines(previous);
    VE_ASSERT_EQ(output.size(), size_t(1));
    if (!output.empty()) VE_ASSERT_EQ(output.front(), std::string("still usable"));
}

VE_TEST(runtime_periodic_and_severity_flush_reach_file) {
    VE_ASSERT(setupJson(R"({"log":{"async":true,"flush_interval_seconds":1,
        "flush_level":"off","file":{"pattern":"%v"}}})"));
    auto periodic_path = log::getLogFilePath();
    veLogI << "periodic";
    VE_ASSERT(VE_WAIT([&] { return lines(periodic_path).size() == 1; }, 2500));
    entry::deinit();
    VE_ASSERT(_disable_logging());
    VE_ASSERT(setupJson(R"({"log":{"async":true,"flush_interval_seconds":0,
        "flush_level":"error","file":{"pattern":"%v"}}})"));
    auto severity_path = log::getLogFilePath();
    veLogI << "buffered";
    veLogE << "flush on error";
    VE_ASSERT(VE_WAIT([&] { return lines(severity_path).size() == 2; }));
    entry::deinit();
    VE_ASSERT(_disable_logging());
}

VE_TEST(runtime_sync_filtering_skips_formatting_and_honors_file_level) {
    VE_ASSERT(setupJson(R"({"log":{"async":false,"flush_interval_seconds":0,
        "file":{"level":"warn","pattern":"%v"}}})"));
    int formatted = 0;
    veLogI << CountFormatting{&formatted};
    veLogIs(CountFormatting{&formatted}, 42);
    veLogD << CountFormatting{&formatted} << std::endl;
    VE_ASSERT_EQ(formatted, 0);
    veLogWs << "accepted" << 42;
    auto path = log::getLogFilePath();
    log::flush();
    auto output = lines(path);
    VE_ASSERT_EQ(output.size(), size_t(1));
    if (!output.empty()) VE_ASSERT_EQ(output.front(), std::string("accepted 42"));
    Node config;
    config.set("log/console/enabled", false);
    config.set("log/dir", (directory().path / "sync_info").string());
    config.set("log/level", "info");
    config.set("log/file/pattern", "%v");
    config.set("log/flush_interval_seconds", 0);
    VE_ASSERT(log::configure(&config));
    veLogI(CountFormatting{&formatted}, "!");
    VE_ASSERT_EQ(formatted, 1);
    auto current = log::getLogFilePath();
    entry::deinit();
    VE_ASSERT(_disable_logging());
    VE_ASSERT_EQ(lines(path).size(), size_t(1));
    auto updated = lines(current);
    VE_ASSERT_EQ(updated.size(), size_t(1));
    if (!updated.empty()) VE_ASSERT_EQ(updated.front(), std::string("formatted!"));
}

VE_TEST(runtime_disabled_file_creates_no_directory_and_can_be_enabled) {
    Node overrides;
    auto dir = directory().path / "disabled";
    overrides.at("log/dir")->set(dir.string());
    VE_ASSERT(setupJson(R"({"log":{"file":{"enabled":false,"pattern":"%v"}}})", &overrides));
    veLogE << "discarded";
    VE_ASSERT(log::getLogFilePath().empty());
    VE_ASSERT(!fs::exists(dir));
    Node config;
    config.set("log/dir", dir.string());
    config.set("log/console/enabled", false);
    config.set("log/file/enabled", true);
    config.set("log/file/pattern", "%v");
    VE_ASSERT(log::configure(&config));
    veLogI << "enabled";
    auto path = log::getLogFilePath();
    entry::deinit();
    VE_ASSERT(_disable_logging());
    auto output = lines(path);
    VE_ASSERT_EQ(output.size(), size_t(1));
    if (!output.empty()) VE_ASSERT_EQ(output.front(), std::string("enabled"));
}

VE_TEST(runtime_reconfigure_drains_previous_queue_and_changes_directory) {
    VE_ASSERT(setupJson(R"({"log":{"async":true,"worker_threads":1,"queue_size":32,
        "flush_interval_seconds":0,"file":{"pattern":"%v"}}})"));
    auto previous = log::getLogFilePath();
    for (int i = 0; i < 1000; ++i) { veLogI << "queued " << i; }
    VE_ASSERT(setupJson(R"({"log":{"async":true,"flush_interval_seconds":0,"file":{"pattern":"%v"}}})"));
    VE_ASSERT_EQ(lines(previous).size(), size_t(1000));
    veLogI << "new directory";
    auto current = log::getLogFilePath();
    entry::deinit();
    VE_ASSERT(_disable_logging());
    VE_ASSERT_EQ(lines(current).size(), size_t(1));
}

VE_TEST(runtime_json_can_disable_both_outputs_and_log_levels) {
    VE_ASSERT(setupJson(R"({"log":{"level":"off","console":{"enabled":false},"file":{"enabled":false}}})"));
    int formatted = 0;
    veLogS << CountFormatting{&formatted};
    VE_ASSERT_EQ(formatted, 0);
    VE_ASSERT(log::getLogFilePath().empty());
    entry::deinit();
    VE_ASSERT(_disable_logging());
}


VE_TEST(runtime_minimal_and_full_reference_have_equal_log_defaults) {
    for (const char* name : {"ve.json", "ve_full.json"}) {
        std::ifstream source(fs::path(VE_CONFIG_SOURCE_DIR) / name);
        std::string json{std::istreambuf_iterator<char>(source), std::istreambuf_iterator<char>()};
        VE_ASSERT(!json.empty());
        VE_ASSERT(setupJson(json));
        veLogD << "filtered default";
        veLogI << "accepted default";
        auto path = log::getLogFilePath();
        entry::deinit();
        VE_ASSERT(_disable_logging());
        auto output = lines(path);
        VE_ASSERT_EQ(output.size(), size_t(1));
        if (!output.empty()) {
            VE_ASSERT(output.front().find("I[") == 0);
            VE_ASSERT(output.front().find("accepted default") != std::string::npos);
        }
    }
}

VE_TEST(runtime_automatic_setup_uses_ve_json_and_ignores_full_reference) {
    auto previous = fs::current_path();
    struct RestoreCwd
    {
        fs::path path;
        ~RestoreCwd() { fs::current_path(path); }
    } restore{previous};
    auto cwd = directory().path / "auto_config";
    fs::create_directories(cwd);
    fs::current_path(cwd);
    { std::ofstream file("ve.json"); file << R"({"app":"automatic","log":{"level":"warn"}})"; }
    { std::ofstream file("ve_full.json"); file << R"({"version":999,"log":{"level":"off"}})"; }
    int sequence = 0;
    auto setup = [&] {
        node::root()->erase("ve/entry");
        Node options;
        options.at("blacklist")->append("")->set("ve");
        options.at("log/console/enabled")->set(false);
        options.set("log/dir", (cwd / ("output_" + std::to_string(++sequence))).string());
        if (!entry::setup(&options)) return false;
        entry::init();
        return true;
    };
    VE_ASSERT(setup());
    VE_ASSERT_EQ(entry::appName(), std::string("automatic"));
    veLogI << "filtered by ve.json";
    veLogW << "accepted by ve.json";
    auto configured = log::getLogFilePath();
    entry::deinit();
    VE_ASSERT(_disable_logging());
    VE_ASSERT_EQ(lines(configured).size(), size_t(1));
    fs::remove("ve.json");
    VE_ASSERT(setup());
    veLogD << "filtered default";
    veLogI << "accepted default";
    auto defaults = log::getLogFilePath();
    entry::deinit();
    VE_ASSERT(_disable_logging());
    VE_ASSERT_EQ(lines(defaults).size(), size_t(1));
}

VE_TEST(runtime_configure_replaces_complete_snapshot_without_retaining_node) {
#ifdef __linux__
    auto threadCount = [] { return std::distance(fs::directory_iterator("/proc/self/task"), fs::directory_iterator{}); };
    auto before = threadCount();
#endif
    std::string previous;
    {
        Node config;
        config.set("log/dir", (directory().path / "snapshot_async").string());
        config.set("log/console/enabled", false);
        config.set("log/async", true);
        config.set("log/queue_size", 32);
        config.set("log/worker_threads", 2);
        config.set("log/flush_interval_seconds", 0);
        config.set("log/flush_level", "off");
        config.set("log/level", "error");
        config.set("log/file/level", "debug");
        config.set("log/file/pattern", "%v");
        VE_ASSERT(log::configure(&config));
        VE_ASSERT(config.get("log/file/level").toString() == "debug");
        VE_ASSERT(!config.find("log/console/level"));
        previous = log::getLogFilePath();
    }
    for (int i = 0; i < 1000; ++i) { veLogD << "queued " << i; }
    {
        Node config;
        config.set("log/dir", (directory().path / "snapshot_defaults").string());
        config.set("log/console/enabled", false);
        config.set("log/flush_interval_seconds", 0);
        VE_ASSERT(log::configure(&config));
    }
#ifdef __linux__
    VE_ASSERT_EQ(threadCount(), before);
#endif
    VE_ASSERT_EQ(lines(previous).size(), size_t(1000));
    int formatted = 0;
    veLogD << CountFormatting{&formatted};
    VE_ASSERT_EQ(formatted, 0);
    veLogI << CountFormatting{&formatted};
    VE_ASSERT_EQ(formatted, 1);
    auto current = log::getLogFilePath();
    log::flush();
    auto output = lines(current);
    VE_ASSERT_EQ(output.size(), size_t(1));
    if (!output.empty()) {
        VE_ASSERT(output.front().find("I[") == 0);
        VE_ASSERT(output.front().find("formatted") != std::string::npos);
    }
    VE_ASSERT(_disable_logging());
}
