// ----------------------------------------------------------------------------
// entry.cpp - ve::entry, ve::version, ve::plugin implementations
// ----------------------------------------------------------------------------
#include "ve/entry.h"
#include "ve/core/loop.h"
#include "ve/core/schema.h"
#include "ve/core/impl/json.h"
#include "ve/core/log.h"

#include <fstream>
#include <algorithm>
#include <condition_variable>
#include <queue>

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

    // Default main-loop blocking (used when no custom runner is set)
    std::mutex              quit_mtx;
    std::condition_variable quit_cv;
    bool                    quit_requested = false;
    int                     exit_code = 0;

    // Custom main-loop runner (set by ve.qt etc. via loop::setMainRunner)
    loop::RunFunc  custom_run;
    loop::QuitFunc custom_quit;
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

std::string keyToPath(const std::string& key)
{
    std::string path = key;
    for (auto& c : path) {
        if (c == '.') c = '/';
    }
    return path;
}

std::string parentKey(const std::string& key)
{
    auto pos = key.rfind('.');
    return (pos == std::string::npos) ? std::string{} : key.substr(0, pos);
}

int keyDepth(const std::string& key)
{
    int d = 0;
    for (char c : key) {
        if (c == '.') ++d;
    }
    return d;
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

    Node* root = node::root();

    if (!options.config_file.empty()) {
        std::string content = readFile(options.config_file);
        if (!content.empty()) {
            if (!schema::importAs<schema::Json>(root, content)) {
                veLogE << "[ve::entry] Failed to parse config: " << options.config_file;
            } else {
                veLogI << "[ve::entry] Config loaded: " << options.config_file;
            }
        } else {
            veLogW << "[ve::entry] Config file empty or not found: " << options.config_file;
        }
    }

    if (n("ve/entry")->get("verbose").toBool(options.verbose)) g.options.verbose = true;
    if (options.terminal) {
        n("ve/client/terminal/stdio/enabled")->set(Var(true));
    }

    g.state = SETUP;
    if (g.options.verbose) veLogI << "[ve::entry] setup complete";
}

void setup(Node* config_node)
{
    auto& g = G();

    Node* root = node::root();
    if (config_node && config_node != root) {
        std::string exported = schema::exportAs<schema::Json>(config_node);
        schema::importAs<schema::Json>(root, exported);
    }

    g.options.verbose = n("ve/entry")->get("verbose").toBool(false);

    g.state = SETUP;
    if (g.options.verbose) veLogI << "[ve::entry] setup complete (from node)";
}

// --- init ------------------------------------------------------------------

static void tryLoadOnePluginSpec(Node* spec_node)
{
    Node* path_node = spec_node->find("path");
    if (!path_node) {
        return;
    }
    std::string path = path_node->getString("");
    if (path.empty()) {
        return;
    }

    if (!spec_node->get("enabled").toBool(true)) {
        return;
    }

    int min_api = spec_node->get("min_api").toInt(0);

    if (!plugin::load(path)) {
        veLogE << "[ve::entry] Plugin load failed: " << path;
        return;
    }

    if (min_api > 0) {
        std::string pname;
        if (Node* name_node = spec_node->find("name")) {
            pname = name_node->getString("");
        }
        if (pname.empty()) {
            pname = path;
        }
        if (!version::check(pname, min_api)) {
            veLogE << "[ve::entry] Plugin " << pname
                   << " version check failed (min_api=" << min_api << ")";
        }
    }
}

static void loadPlugins()
{
    Node* entry_node = node::root()->find("ve/entry");
    if (!entry_node) {
        return;
    }
    Node* plugins_root = entry_node->find("plugins");
    if (!plugins_root) {
        return;
    }

    if (plugins_root->find("path")) {
        // "plugins": { "path": "...", "enabled": true, ... }
        tryLoadOnePluginSpec(plugins_root);
    } else {
        // "plugins": [ { ... }, { ... } ] -> plugins/#0, plugins/#1, ...
        for (Node* pn : plugins_root->children()) {
            tryLoadOnePluginSpec(pn);
        }
    }
}

static void buildModuleGraph(Vector<ModuleSlot>& slots)
{
    auto& factory = globalModuleFactory();
    auto& pri_map = globalModulePriority();

    Hash<int> base_pri;
    for (const auto& kv : factory) {
        const std::string& key = kv.key;

        int priority = pri_map.has(key) ? pri_map[key] : 100;
        bool enabled = true;

        Node* mn = node::root()->find(keyToPath(key));
        if (mn) {
            enabled = mn->get("enabled").toBool(true);
            priority = mn->get("priority").toInt(priority);
        } else if (key.rfind("ve.service.", 0) == 0) {
            // Network service modules are opt-in: require a matching subtree in config (e.g. ve.json).
            enabled = false;
        }

        if (!enabled) continue;
        base_pri[key] = priority;
    }

    // Enforce parent priority constraint: child.effective >= nearest ancestor.effective.
    // Sort by depth first so parents are resolved before children.
    Vector<std::string> keys;
    keys.reserve(base_pri.size());
    for (auto it = base_pri.begin(); it != base_pri.end(); ++it) {
        keys.push_back(it->first);
    }
    std::stable_sort(keys.begin(), keys.end(),
        [](const std::string& a, const std::string& b) {
            return keyDepth(a) < keyDepth(b);
        });

    Hash<int> eff_pri;
    for (auto& key : keys) {
        int own = base_pri[key];
        std::string pk = parentKey(key);
        while (!pk.empty()) {
            auto pit = eff_pri.find(pk);
            if (pit != eff_pri.end()) {
                if (own < pit->second) own = pit->second;
                break;
            }
            pk = parentKey(pk);
        }
        eff_pri[key] = own;
    }

    for (auto& key : keys) {
        ModuleSlot slot;
        slot.key = key;
        slot.priority = eff_pri[key];
        slots.push_back(std::move(slot));
    }

    std::stable_sort(slots.begin(), slots.end(),
        [](const ModuleSlot& a, const ModuleSlot& b) {
            if (a.priority != b.priority) return a.priority < b.priority;
            return keyDepth(a.key) < keyDepth(b.key);
        });
}

static void resolveDepends(Vector<ModuleSlot>& slots)
{
    Hash<int> key_to_idx;
    for (int i = 0; i < (int)slots.size(); ++i) {
        key_to_idx[slots[i].key] = i;
    }

    int n_slots = (int)slots.size();
    Vector<Vector<int>> adj(n_slots);
    Vector<int> indegree(n_slots, 0);

    // Implicit parent -> child edges (child depends on nearest registered ancestor)
    for (int i = 0; i < n_slots; ++i) {
        std::string pk = parentKey(slots[i].key);
        while (!pk.empty()) {
            auto pit = key_to_idx.find(pk);
            if (pit != key_to_idx.end()) {
                adj[pit->second].push_back(i);
                indegree[i]++;
                break;
            }
            pk = parentKey(pk);
        }
    }

    // Explicit depends edges from config
    for (auto& slot : slots) {
        Node* mn = node::root()->find(keyToPath(slot.key));
        if (!mn) continue;
        auto* deps_n = mn->find("depends");
        if (!deps_n) continue;

        auto it = key_to_idx.find(slot.key);
        if (it == key_to_idx.end()) continue;
        int to = it->second;

        for (auto* dep : *deps_n) {
            std::string dep_key = dep->getString();
            auto dit = key_to_idx.find(dep_key);
            if (dit == key_to_idx.end()) {
                veLogW << "[ve::entry] Dependency not found: " << slot.key << " -> " << dep_key;
                continue;
            }
            adj[dit->second].push_back(to);
            indegree[to]++;
        }
    }

    // Kahn's topo sort with priority queue (min-heap) for deterministic ordering
    auto cmp = [&](int a, int b) {
        if (slots[a].priority != slots[b].priority) return slots[a].priority > slots[b].priority;
        int da = keyDepth(slots[a].key), db = keyDepth(slots[b].key);
        if (da != db) return da > db;
        return a > b;
    };
    std::priority_queue<int, std::vector<int>, decltype(cmp)> pq(cmp);

    for (int i = 0; i < n_slots; ++i) {
        if (indegree[i] == 0) pq.push(i);
    }

    Vector<int> order;
    order.reserve(n_slots);
    while (!pq.empty()) {
        int u = pq.top(); pq.pop();
        order.push_back(u);
        for (int v : adj[u]) {
            if (--indegree[v] == 0) pq.push(v);
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
    bool verbose = g.options.verbose;

    loadPlugins();

    buildModuleGraph(g.modules);
    resolveDepends(g.modules);

    auto& factory = globalModuleFactory();

    for (auto& slot : g.modules) {
        if (verbose) {
            veLogI << "[ve::entry] Creating module: " << slot.key;
        }
        try {
            slot.instance = factory.produce(slot.key);
        } catch (const std::exception& e) {
            veLogE << "[ve::entry] Module create failed (" << slot.key << "): " << e.what();
        }
        if (slot.instance && verbose) {
            veLogI << "[ve::entry] Module created: " << slot.key;
        }
    }

    g.state = INIT;
    for (auto& slot : g.modules) {
        if (!slot.instance) continue;
        if (verbose) {
            veLogI << "[ve::entry] INIT: " << slot.key;
        }
        slot.instance->exeState<Module::INIT>();
    }

    if (verbose) {
        veLogI << "[ve::entry] " << g.modules.size() << " modules initialized";
    }

    // ready(): reverse order (children first, parents last)
    for (int i = (int)g.modules.size() - 1; i >= 0; --i) {
        auto& slot = g.modules[i];
        if (!slot.instance) continue;
        if (verbose) {
            veLogI << "[ve::entry] READY: " << slot.key;
        }
        slot.instance->exeState<Module::READY>();
    }

    g.state = READY;

    if (verbose) {
        veLogI << "[ve::entry] " << g.modules.size() << " modules ready";
    }
}

// --- run -------------------------------------------------------------------

int run()
{
    auto& g = G();
    g.state = RUNNING;
    return loop::run();
}

void requestQuit(int exit_code)
{
    loop::quit(exit_code);
}

// --- deinit ----------------------------------------------------------------

void deinit()
{
    auto& g = G();
    bool verbose = g.options.verbose;

    for (int i = (int)g.modules.size() - 1; i >= 0; --i) {
        auto& slot = g.modules[i];
        if (!slot.instance) continue;
        if (verbose) {
            veLogI << "[ve::entry] DEINIT: " << slot.key;
        }
        slot.instance->exeState<Module::DEINIT>();
    }

    // destructor: reverse order (children first, parents last)
    for (int i = (int)g.modules.size() - 1; i >= 0; --i) {
        delete g.modules[i].instance;
        g.modules[i].instance = nullptr;
    }
    g.modules.clear();

    g.state = SHUTDOWN;
    g.quit_requested = false;
    g.custom_run = nullptr;
    g.custom_quit = nullptr;

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
    opts.argc = argc;
    opts.argv = argv;

    Vector<std::string> plugin_args;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
            opts.config_file = argv[++i];
        } else if (arg == "--verbose" || arg == "-v") {
            opts.verbose = true;
        } else if (arg == "--terminal" || arg == "-t") {
            opts.terminal = true;
        } else if (arg.rfind("--set=", 0) == 0) {
            std::string kv = arg.substr(6);
            auto eq = kv.find('=');
            if (eq != std::string::npos) {
                std::string path = kv.substr(0, eq);
                std::string val  = kv.substr(eq + 1);
                n("ve/entry/" + path)->set(Var(val));
            }
        } else if (arg[0] != '-') {
            if (endsWith(arg, ".dll") || endsWith(arg, ".so") || endsWith(arg, ".dylib")) {
                plugin_args.push_back(arg);
            } else if (opts.config_file.empty()) {
                opts.config_file = arg;
            }
        }
    }

    if (opts.config_file.empty()) {
        opts.config_file = "ve.json";
    }

    setup(opts);

    for (auto& p : plugin_args) {
        Node* plugins_node = n("ve/entry")->at("plugins");
        auto* pn = plugins_node->append("");
        pn->at("path")->set(Var(p));
    }

    init();
    int code = run();
    deinit();
    return code;
}

// --- queries ---------------------------------------------------------------

State state() { return G().state; }

const Options& options() { return G().options; }

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

#ifdef _WIN32
static std::string dirnameOfHostExe()
{
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (!n || n >= MAX_PATH) {
        return {};
    }
    std::string p(buf, buf + n);
    size_t slash = p.find_last_of("\\/");
    if (slash == std::string::npos) {
        return {};
    }
    return p.substr(0, slash);
}
#endif

bool load(const std::string& path)
{
    void* handle = nullptr;

#ifdef _WIN32
    HMODULE mod = LoadLibraryA(path.c_str());
    DWORD err = mod ? 0 : GetLastError();
    if (!mod && path.find_first_of("/\\") == std::string::npos) {
        std::string dir = dirnameOfHostExe();
        if (!dir.empty()) {
            std::string full = dir + '\\' + path;
            mod = LoadLibraryA(full.c_str());
            if (mod) {
                err = 0;
            }
        }
    }
    if (!mod) {
        veLogE << "[ve::plugin] LoadLibrary failed: " << path
               << " (error " << err << ")";
        return false;
    }
    handle = (void*)mod;
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

// ============================================================================
// loop:: main runner
// ============================================================================

namespace loop {

int run()
{
    auto& g = G();
    if (g.custom_run) {
        return g.custom_run();
    }
    // Default: block on condition_variable
    {
        std::unique_lock<std::mutex> lock(g.quit_mtx);
        g.quit_cv.wait(lock, [&] { return g.quit_requested; });
    }
    return g.exit_code;
}

void quit(int exit_code)
{
    auto& g = G();
    if (g.custom_quit) {
        g.exit_code = exit_code;
        g.custom_quit(exit_code);
        return;
    }
    // Default: notify condition_variable
    {
        std::lock_guard<std::mutex> lock(g.quit_mtx);
        g.exit_code = exit_code;
        g.quit_requested = true;
    }
    g.quit_cv.notify_all();
}

void setMainRunner(RunFunc run_fn, QuitFunc quit_fn)
{
    auto& g = G();
    g.custom_run = std::move(run_fn);
    g.custom_quit = std::move(quit_fn);
}

} // namespace loop

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
