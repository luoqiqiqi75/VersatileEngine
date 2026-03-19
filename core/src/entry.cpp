// ----------------------------------------------------------------------------
// entry.cpp - ve::entry, ve::version, ve::plugin implementations
// ----------------------------------------------------------------------------
#include "ve/entry.h"
#include "ve/core/schema.h"
#include "ve/core/impl/json.h"
#include "ve/core/log.h"

#include <fstream>
#include <algorithm>
#include <condition_variable>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace ve {

// ============================================================================
// Internal state
// ============================================================================

namespace {

struct ModuleSlot {
    std::string key;
    int         priority = 100;
    Module*     instance = nullptr;
};

struct EntryState {
    entry::State      state = entry::NONE;
    entry::Options    options;
    Vector<ModuleSlot> modules;

    std::mutex              quit_mtx;
    std::condition_variable quit_cv;
    bool                    quit_requested = false;
    int                     exit_code = 0;
};

EntryState& G()
{
    static EntryState s;
    return s;
}

std::string readFile(const std::string& path)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return {};
    return {std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>()};
}

bool endsWith(const std::string& s, const std::string& suffix)
{
    if (suffix.size() > s.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin(),
        [](char a, char b) { return std::tolower(a) == std::tolower(b); });
}

} // anon

// ============================================================================
// ve::entry
// ============================================================================

namespace entry {

// --- setup -----------------------------------------------------------------

void setup(const std::string& config_file)
{
    Options opts;
    opts.config_file = config_file;
    setup(opts);
}

void setup(const Options& options)
{
    auto& g = G();
    g.options = options;

    Node* entry_cfg = n("ve/entry");

    if (!options.config_file.empty()) {
        std::string content = readFile(options.config_file);
        if (!content.empty()) {
            if (!schema::importAs<schema::Json>(entry_cfg, content)) {
                veLogE << "[ve::entry] Failed to parse config: " << options.config_file;
            } else {
                veLogI << "[ve::entry] Config loaded: " << options.config_file;
            }
        } else {
            veLogW << "[ve::entry] Config file empty or not found: " << options.config_file;
        }
    }

    if (options.verbose) {
        n("ve/entry/verbose")->set(Var(true));
    }

    g.state = SETUP;
    n("ve/state")->set(Var("SETUP"));
    veLogI << "[ve::entry] setup complete";
}

void setup(Node* config_node)
{
    auto& g = G();

    Node* entry_cfg = n("ve/entry");
    if (config_node && config_node != entry_cfg) {
        std::string exported = schema::exportAs<schema::Json>(config_node);
        schema::importAs<schema::Json>(entry_cfg, exported);
    }

    g.state = SETUP;
    n("ve/state")->set(Var("SETUP"));
    veLogI << "[ve::entry] setup complete (from node)";
}

// --- init ------------------------------------------------------------------

static void loadPlugins()
{
    // Plugins only load when explicitly listed in config
    Node* plugins_node = n("ve/entry")->resolve("plugins");
    if (!plugins_node || plugins_node->count() == 0) return;

    for (auto* pn : *plugins_node) {
        auto* path_n = pn->resolve("path");
        if (!path_n) continue;

        std::string path = path_n->get<std::string>();
        if (path.empty()) continue;

        // enabled defaults to true when listed in config (explicit opt-in by presence)
        bool enabled = pn->resolve("enabled")
            ? pn->getAt<bool>("enabled", true) : true;
        if (!enabled) continue;

        int min_api = pn->resolve("min_api")
            ? pn->getAt<int>("min_api", 0) : 0;

        if (!plugin::load(path)) {
            veLogE << "[ve::entry] Plugin load failed: " << path;
            continue;
        }

        if (min_api > 0) {
            std::string pname = pn->resolve("name")
                ? pn->getAt<std::string>("name") : path;
            if (!version::check(pname, min_api)) {
                veLogE << "[ve::entry] Plugin " << pname
                       << " version check failed (min_api=" << min_api << ")";
            }
        }
    }
}

static void buildModuleGraph(Vector<ModuleSlot>& slots)
{
    auto& factory = globalModuleFactory();
    Node* mod_root = n("ve/entry")->resolve("module");

    auto& pri_map = globalModulePriority();

    for (const auto& kv : factory) {
        const std::string& key = kv.key;

        int priority = pri_map.has(key) ? pri_map[key] : 100;
        bool enabled = true;

        Node* mn = mod_root ? mod_root->resolve(key) : nullptr;
        if (mn) {
            auto* en = mn->resolve("enabled");
            if (en) enabled = en->get<bool>(true);
            auto* pn = mn->resolve("priority");
            if (pn) priority = pn->get<int>(priority);
        }

        if (!enabled) continue;

        ModuleSlot slot;
        slot.key = key;
        slot.priority = priority;
        slots.push_back(std::move(slot));
    }

    if (mod_root) {
        for (auto* child : *mod_root) {
            if (!factory.has(child->name())) {
                veLogW << "[ve::entry] Config references unregistered module: " << child->name();
            }
        }
    }

    std::stable_sort(slots.begin(), slots.end(),
        [](const ModuleSlot& a, const ModuleSlot& b) {
            return a.priority < b.priority;
        });
}

static void resolveDepends(Vector<ModuleSlot>& slots)
{
    Node* mod_root = n("ve/entry")->resolve("module");
    if (!mod_root) return;

    Hash<int> key_to_idx;
    for (int i = 0; i < (int)slots.size(); ++i) {
        key_to_idx[slots[i].key] = i;
    }

    int n_slots = (int)slots.size();
    Vector<Vector<int>> adj(n_slots);
    Vector<int> indegree(n_slots, 0);

    for (auto& slot : slots) {
        Node* mn = mod_root->resolve(slot.key);
        if (!mn) continue;
        auto* deps_n = mn->resolve("depends");
        if (!deps_n) continue;

        auto it = key_to_idx.find(slot.key);
        if (it == key_to_idx.end()) continue;
        int to = it->second;

        for (auto* dep : *deps_n) {
            std::string dep_key = dep->get<std::string>();
            auto dit = key_to_idx.find(dep_key);
            if (dit == key_to_idx.end()) {
                veLogW << "[ve::entry] Dependency not found: " << slot.key << " -> " << dep_key;
                continue;
            }
            adj[dit->second].push_back(to);
            indegree[to]++;
        }
    }

    Vector<int> queue;
    for (int i = 0; i < n_slots; ++i) {
        if (indegree[i] == 0) queue.push_back(i);
    }

    Vector<int> order;
    order.reserve(n_slots);
    int head = 0;
    while (head < (int)queue.size()) {
        int u = queue[head++];
        order.push_back(u);
        for (int v : adj[u]) {
            if (--indegree[v] == 0) queue.push_back(v);
        }
    }

    if ((int)order.size() != n_slots) {
        veLogE << "[ve::entry] Circular dependency detected in module graph!";
        return;
    }

    Vector<ModuleSlot> sorted;
    sorted.reserve(n_slots);
    for (int idx : order) {
        sorted.push_back(std::move(slots[idx]));
    }
    slots = std::move(sorted);
}

void init()
{
    auto& g = G();
    bool verbose = n("ve/entry/verbose")->get<bool>();

    loadPlugins();

    buildModuleGraph(g.modules);
    resolveDepends(g.modules);

    auto& factory = globalModuleFactory();

    // Instantiate - each module's constructor reads its node() config
    for (auto& slot : g.modules) {
        if (verbose) {
            veLogI << "[ve::entry] Creating module: " << slot.key;
        }
        try {
            slot.instance = factory.produce(slot.key);
        } catch (const std::exception& e) {
            veLogE << "[ve::entry] Module create failed (" << slot.key << "): " << e.what();
        }
        if (slot.instance) {
            slot.instance->node()->set("state", Var("NONE"));
            if (verbose) {
                veLogI << "[ve::entry] Module created: " << slot.key;
            }
        }
    }

    // INIT phase
    g.state = INIT;
    n("ve/state")->set(Var("INIT"));
    for (auto& slot : g.modules) {
        if (!slot.instance) continue;
        if (verbose) {
            veLogI << "[ve::entry] INIT: " << slot.key;
        }
        slot.instance->exeState<Module::INIT>();
        slot.instance->node()->set("state", Var("INIT"));
    }

    // READY phase
    for (auto& slot : g.modules) {
        if (!slot.instance) continue;
        if (verbose) {
            veLogI << "[ve::entry] READY: " << slot.key;
        }
        slot.instance->exeState<Module::READY>();
        slot.instance->node()->set("state", Var("READY"));
    }

    g.state = READY;
    n("ve/state")->set(Var("READY"));

    if (verbose) {
        veLogI << "[ve::entry] init complete (" << g.modules.size() << " modules)";
    }
}

// --- run -------------------------------------------------------------------

int run()
{
    auto& g = G();
    g.state = RUNNING;
    n("ve/state")->set(Var("RUNNING"));

    {
        std::unique_lock<std::mutex> lock(g.quit_mtx);
        g.quit_cv.wait(lock, [&] { return g.quit_requested; });
    }

    return g.exit_code;
}

void requestQuit(int exit_code)
{
    auto& g = G();
    {
        std::lock_guard<std::mutex> lock(g.quit_mtx);
        g.exit_code = exit_code;
        g.quit_requested = true;
    }
    g.quit_cv.notify_all();
}

// --- deinit ----------------------------------------------------------------

void deinit()
{
    auto& g = G();
    bool verbose = n("ve/entry/verbose")->get<bool>();

    // DEINIT in reverse order
    for (int i = (int)g.modules.size() - 1; i >= 0; --i) {
        auto& slot = g.modules[i];
        if (!slot.instance) continue;
        if (verbose) {
            veLogI << "[ve::entry] DEINIT: " << slot.key;
        }
        slot.instance->exeState<Module::DEINIT>();
        slot.instance->node()->set("state", Var("DEINIT"));
    }

    // Delete module instances
    for (auto& slot : g.modules) {
        delete slot.instance;
        slot.instance = nullptr;
    }
    g.modules.clear();

    g.state = SHUTDOWN;
    n("ve/state")->set(Var("SHUTDOWN"));
    g.quit_requested = false;

    if (verbose) {
        veLogI << "[ve::entry] deinit complete";
    }
}

// --- convenience -----------------------------------------------------------

int exec(const std::string& config_file)
{
    setup(config_file);
    init();
    int code = run();
    deinit();
    return code;
}

int exec(int argc, char** argv)
{
    Options opts;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--config" || arg == "-c") && i + 1 < argc)
            opts.config_file = argv[++i];
        else if (arg == "--verbose" || arg == "-v")
            opts.verbose = true;
        else if (arg.rfind("--set=", 0) == 0) {
            // --set=path=value → set node value
            std::string kv = arg.substr(6);
            auto eq = kv.find('=');
            if (eq != std::string::npos) {
                std::string path = kv.substr(0, eq);
                std::string val  = kv.substr(eq + 1);
                n("ve/entry/" + path)->set(Var(val));
            }
        }
        else if (i == 1 && arg[0] != '-') {
            // First positional arg is config file
            opts.config_file = arg;
        }
    }

    if (opts.config_file.empty())
        opts.config_file = "ve.json";

    setup(opts);
    init();
    int code = run();
    deinit();
    return code;
}

// --- queries ---------------------------------------------------------------

State state() { return G().state; }

Node* config() { return n("ve"); }

} // namespace entry

// ============================================================================
// ve::plugin - cross-platform dynamic library loading
// ============================================================================

namespace plugin {

static Vector<Info>& pluginList()
{
    static Vector<Info> list;
    return list;
}

bool load(const std::string& path)
{
    void* handle = nullptr;

#ifdef _WIN32
    handle = (void*)LoadLibraryA(path.c_str());
    if (!handle) {
        veLogE << "[ve::plugin] LoadLibrary failed: " << path
               << " (error " << GetLastError() << ")";
        return false;
    }
#else
    handle = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        veLogE << "[ve::plugin] dlopen failed: " << path
               << " (" << dlerror() << ")";
        return false;
    }
#endif

    // Extract name from path
    std::string name = path;
    auto slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    auto dot = name.rfind('.');
    if (dot != std::string::npos) name = name.substr(0, dot);

    Info info;
    info.path   = path;
    info.name   = name;
    info.handle = handle;
    info.api_version = version::manager().has(name) ? version::number(name) : 0;

    pluginList().push_back(std::move(info));
    veLogI << "[ve::plugin] Loaded: " << path;
    return true;
}

bool unload(const std::string& name)
{
    auto& list = pluginList();
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (it->name == name) {
            if (it->handle) {
#ifdef _WIN32
                FreeLibrary((HMODULE)it->handle);
#else
                dlclose(it->handle);
#endif
            }
            veLogI << "[ve::plugin] Unloaded: " << name;
            list.erase(it);
            return true;
        }
    }
    return false;
}

const Vector<Info>& loaded()
{
    return pluginList();
}

} // namespace plugin

} // namespace ve

// ============================================================================
// operator<< for entry::State
// ============================================================================

std::ostream& operator<<(std::ostream& os, ve::entry::State s)
{
    switch (s) {
        case ve::entry::NONE:     os << "NONE";     break;
        case ve::entry::SETUP:    os << "SETUP";    break;
        case ve::entry::INIT:     os << "INIT";     break;
        case ve::entry::READY:    os << "READY";    break;
        case ve::entry::RUNNING:  os << "RUNNING";  break;
        case ve::entry::SHUTDOWN: os << "SHUTDOWN";  break;
    }
    return os;
}
