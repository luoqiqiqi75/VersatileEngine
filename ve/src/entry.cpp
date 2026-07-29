// ----------------------------------------------------------------------------
// entry.cpp - ve::entry, ve::version, ve::plugin implementations
// ----------------------------------------------------------------------------
#include "ve/entry.h"
#include "ve/core/loop.h"
#include "ve/core/schema.h"
#include "ve/core/impl/json.h"
#include "ve/core/log.h"

#include <fstream>
#include <iostream>
#include <queue>
#include <filesystem>
#include <thread>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

#define VE_MIN_API 0

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
    entry::State       state = entry::NONE;
    Vector<ModuleSlot> modules;

    int                argc = 0;
    char**             argv = nullptr;
    std::string        app_name;
    bool               verbose = false;
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

/// Avoid treating the next argv after `-r` as host:port when it is clearly a config or plugin path
/// (e.g. `ve.exe -r ve.json` must still load ve.json as config).
bool looksLikeConfigOrPluginArg(std::string_view arg)
{
    std::string s(arg);
    return endsWith(s, ".json") || endsWith(s, ".dll") || endsWith(s, ".so") || endsWith(s, ".dylib");
}

bool isPluginPath(const std::string& s)
{
    return endsWith(s, ".dll") || endsWith(s, ".so") || endsWith(s, ".dylib");
}

bool parseHostPort(std::string_view input, std::string& host, int& port)
{
    if (input.empty()) {
        return false;
    }

    std::string value(input);
    size_t colon = value.rfind(':');
    if (colon == std::string::npos) {
        host = value;
        return !host.empty();
    }

    std::string maybe_port = value.substr(colon + 1);
    if (maybe_port.empty()) {
        return false;
    }
    if (!std::all_of(maybe_port.begin(), maybe_port.end(), [](char ch) { return ch >= '0' && ch <= '9'; })) {
        host = value;
        return !host.empty();
    }

    host = value.substr(0, colon);
    if (host.empty()) {
        return false;
    }
    port = std::stoi(maybe_port);
    return port > 0 && port <= 65535;
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

} // anon

// ============================================================================
// ve::entry
// ============================================================================

namespace entry {

// --- CLI parse -------------------------------------------------------------
//
// Parse argv into a fresh options node, plus fill in argc/argv/app_name on the
// entry state. Every flag writes straight to the path it will occupy on
// /ve/entry, so there is no second representation to keep in sync. Tokens VE
// does not recognize are left alone — they stay in the argv subtree for the
// application to parse, so adding a flag here cannot break a downstream
// launcher. On error prints to stderr and returns false.

namespace {

bool parseArgs(int argc, char** argv, Node* opts_n)
{
    auto& g = G();
    g.argc = argc;
    g.argv = argv;

    if (argc > 0 && argv[0]) {
        namespace fs = std::filesystem;
        g.app_name = fs::path(argv[0]).stem().string();
    }

    // Every token, verbatim — VE-consumed or not.
    Node* argv_n = opts_n->at("argv");
    for (int i = 0; i < argc; ++i) {
        if (argv[i]) argv_n->append("")->set(Var(std::string(argv[i])));
    }

    bool terminal = false;
    bool remote_terminal = false;
    std::string remote_host = "127.0.0.1";
    int remote_port = 10000;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
            opts_n->at("config_file")->set(Var(std::string(argv[++i])));
        } else if (arg == "--verbose" || arg == "-v") {
            opts_n->at("verbose")->set(Var(true));
        } else if (arg == "--terminal" || arg == "--local-terminal" || arg == "-t") {
            terminal = true;
        } else if (arg == "--remote" || arg == "--remote-terminal" || arg == "-r") {
            remote_terminal = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                const char* next = argv[i + 1];
                if (!looksLikeConfigOrPluginArg(next)) {
                    if (!parseHostPort(next, remote_host, remote_port)) {
                        std::cerr << "Invalid remote endpoint: " << next << '\n';
                        return false;
                    }
                    ++i;
                }
            }
        } else if (arg.rfind("--remote=", 0) == 0) {
            remote_terminal = true;
            if (!parseHostPort(arg.substr(9), remote_host, remote_port)) {
                std::cerr << "Invalid remote endpoint: " << arg.substr(9) << '\n';
                return false;
            }
        } else if (arg.rfind("--remote-terminal=", 0) == 0) {
            remote_terminal = true;
            if (!parseHostPort(arg.substr(18), remote_host, remote_port)) {
                std::cerr << "Invalid remote endpoint: " << arg.substr(18) << '\n';
                return false;
            }
        } else if (arg[0] != '-') {
            if (isPluginPath(arg)) {
                opts_n->at("plugins")->append("")->at("path")->set(Var(arg));
            } else if (endsWith(arg, ".json") && !opts_n->find("config_file")) {
                opts_n->at("config_file")->set(Var(arg));
            }
        }
        // Anything else: not ours. It is already in the argv subtree.
    }

    if (terminal && remote_terminal) {
        std::cerr << "Local terminal (-t/--terminal) and remote terminal (-r/--remote) are mutually exclusive.\n";
        return false;
    }
    if (terminal) {
        opts_n->at("modules/ve/client/terminal/stdio/enabled")->set(Var(true));
    }
    if (remote_terminal) {
        Node* tcp = opts_n->at("modules/ve/client/terminal/tcp");
        tcp->at("enabled")->set(Var(true));
        tcp->at("config/host")->set(Var(remote_host));
        tcp->at("config/port")->set(Var(remote_port));
    }
    return true;
}

// Apply /ve/entry/log + app during setup, so the entry pipeline's own logs
// already honor the config. This is the only place log settings are applied.
void applyLogSettings(Node* entry_n)
{
    std::string app = entry_n->get("app").toString();
    if (!app.empty()) {
        G().app_name = app;
    }
    if (!G().app_name.empty()) {
        log::setAppName(G().app_name);
    }

    std::string level = entry_n->get("log/level").toString("info");
    if (level.empty()) level = "info";
    switch (level[0]) {
        case 'd': log::setLevel(LogLevel::Debug);  break;
        case 'w': log::setLevel(LogLevel::Waring); break;
        case 'e': log::setLevel(LogLevel::Error);  break;
        default:  log::setLevel(LogLevel::Info);   break;
    }

    std::string dir = entry_n->get("log/dir").toString();
    if (!dir.empty()) {
        log::setLogDir(dir);
    }
}

} // anon

// --- setup -----------------------------------------------------------------

bool setup(Node* options_n)
{
    auto& g = G();

    // 1. Settings setup() itself needs before it can read anything: where the
    //    config file is, and whether to narrate. Default to "ve.json" so a bare
    //    setup(nullptr) still picks up a local file.
    std::string config_file;
    bool verbose = false;
    if (options_n) {
        config_file = options_n->get("config_file").toString();
        verbose = options_n->get("verbose").toBool(false);
        g.app_name = options_n->get("app").toString(g.app_name);
    }

    Node* entry_n = n("ve/entry");

    // 2. Load the single startup config file. A missing file is not an error —
    //    every setting has a built-in default.
    namespace fs = std::filesystem;
    std::error_code ec;
    if (config_file.empty() && fs::exists("ve.json", ec)) {
        config_file = "ve.json";
    }
    if (!config_file.empty()) {
        std::string content = readFile(config_file);
        if (content.empty()) {
            veLogW << "[ve/entry] Config file empty or missing: " << config_file;
        } else if (!schema::toNode<schema::JsonS>(entry_n, content)) {
            veLogE << "[ve/entry] Config parse failed: " << config_file;
            return false;
        } else if (verbose) {
            veLogI << "[ve/entry] Config loaded: " << config_file;
        }
    }

    // 3. Refuse a config that asks for a newer VE rather than silently
    //    ignoring the parts this build does not understand.
    int required = entry_n->get("version").toInt(0);
    if (required > VE_ENTRY_VERSION) {
        veLogE << "[ve/entry] Config requires VE version " << required
               << ", this build supports " << VE_ENTRY_VERSION;
        return false;
    }

    // 4. CLI plugins append after file plugins. Every plugin list element is
    // anonymous (#0, #1, ...), so a regular tree copy would otherwise merge
    // the CLI's first element onto the file's first element and silently
    // replace it. Keep the documented load order instead: file entries first,
    // then positional .so/.dll/.dylib arguments.
    if (options_n) {
        if (Node* cli_plugins = options_n->find("plugins")) {
            Node* file_plugins = entry_n->at("plugins");
            for (Node* spec : *cli_plugins) {
                file_plugins->append("")->copy(spec, Node::COPY_STRICT);
            }
            // The list has already been merged above; exclude it from the
            // ordinary CLI overlay below.
            options_n->erase("plugins");
        }
    }

    // 5. Overlay caller options — CLI wins over the file.
    entry_n->copy(options_n);

    // 6. Logging is live from here on.
    applyLogSettings(entry_n);

    g.verbose = entry_n->get("verbose").toBool(false);
    g.state = SETUP;
    if (g.verbose) veLogI << "[ve/entry] setup complete";
    return true;
}

bool setup(int argc, char** argv)
{
    Node opts("options");
    if (!parseArgs(argc, argv, &opts)) {
        veLogE << "[ve/entry] options parse failed!";
        return false;
    }
    return setup(&opts);
}

bool setup(const std::string& config_file)
{
    Node opts("options");
    opts.at("config_file")->set(Var(config_file));
    return setup(&opts);
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
        veLogE << "[ve/entry] Plugin load failed: " << path;
        return;
    }

    if (min_api > VE_MIN_API) {
        std::string pname;
        if (Node* name_node = spec_node->find("name")) {
            pname = name_node->getString("");
        }
        if (pname.empty()) {
            pname = path;
        }
        if (!version::check(pname, min_api)) {
            veLogE << "[ve/entry] Plugin " << pname
                   << " version check failed (min_api=" << min_api << ")";
        }
    }
}

static void loadPlugins()
{
    Node* plugins_root = n("ve/entry")->find("plugins");
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

// Blacklist match: an entry "a.b" bans "a.b" and everything under it
// ("a.b.c", "a.b.c.d", ...). No wildcard syntax.
static bool inBlacklist(const std::string& key, const Vector<std::string>& black)
{
    for (const auto& b : black) {
        if (b.empty()) continue;
        if (key == b) return true;
        if (key.size() > b.size() && key.compare(0, b.size(), b) == 0 && key[b.size()] == '.') {
            return true;
        }
    }
    return false;
}

// Compute the ordered list of module keys to instantiate. Applies the
// blacklist, keeps ve.service.* opt-in (needs a matching config subtree), and
// emits keys in hierarchical DFS pre-order with siblings sorted by base
// priority (parent priority is not inherited — order is scoped to siblings).
static Vector<std::string> selectModuleKeys()
{
    auto& factory = module::factory();

    Vector<std::string> black;
    if (Node* bn = n("ve/entry")->find("blacklist")) {
        for (Node* c : bn->children()) black.push_back(c->getString(""));
    }

    Node* mods_root = n("ve/entry/modules");

    struct Entry {
        std::string key;
        int         priority = 100;
    };

    // Map: parent key -> children entries (root uses empty parent key).
    Hash<Vector<Entry>> tree;
    Hash<int> known;   // membership: key -> 1
    Vector<std::string> raw_keys = factory::keys("module");

    // First pass: filter, then bucket by parent key.
    for (const auto& key : raw_keys) {
        if (inBlacklist(key, black)) continue;

        // ve.service.* is opt-in: require a matching config subtree.
        if (key.rfind("ve.service.", 0) == 0) {
            if (!mods_root->find(keyToPath(key))) continue;
        }

        int priority = 100;
        auto* nd = factory.node(key, VE_FACTORY_KEY_SEP);
        if (nd) {
            if (auto* pn = nd->find("priority")) priority = pn->getInt(100);
        }

        known[key] = 1;
        tree[parentKey(key)].push_back(Entry{key, priority});
    }

    // Re-attach orphaned subtrees: nearest surviving ancestor becomes the parent.
    // This lets a child whose parent is blacklisted still load standalone.
    Hash<Vector<Entry>> tree2;
    for (auto it = tree.begin(); it != tree.end(); ++it) {
        std::string pk = it->first;
        while (!pk.empty() && known.find(pk) == known.end()) {
            pk = parentKey(pk);
        }
        auto& bucket = tree2[pk];
        for (auto& e : it->second) bucket.push_back(std::move(e));
    }

    for (auto it = tree2.begin(); it != tree2.end(); ++it) {
        std::stable_sort(it->second.begin(), it->second.end(),
            [](const Entry& a, const Entry& b) { return a.priority < b.priority; });
    }

    Vector<std::string> ordered;
    ordered.reserve(known.size());
    std::function<void(const std::string&)> emit = [&](const std::string& pk) {
        auto it = tree2.find(pk);
        if (it == tree2.end()) return;
        for (auto& e : it->second) {
            ordered.push_back(e.key);
            emit(e.key);
        }
    };
    emit("");
    return ordered;
}

// Pre-build empty module nodes in the chosen order so the underlying children
// Vector reflects load order; then copy the corresponding config subtree in.
static void buildModuleNodes(const Vector<std::string>& keys)
{
    Node* root = node::root();
    Node* mods = n("ve/entry/modules");

    for (const auto& key : keys) {
        std::string path = keyToPath(key);
        Node* mn = root->atPath(path);
        if (Node* src = mods->find(path)) {
            mn->copy(src);
        }
    }
}

// Populate slots by walking the (already ordered) module subtrees on the real
// node tree. Skips subtrees that were never built (blacklisted / not selected).
static void buildModuleGraph(Vector<ModuleSlot>& slots, const Vector<std::string>& keys)
{
    auto& factory = module::factory();
    slots.reserve(keys.size());
    for (const auto& key : keys) {
        ModuleSlot slot;
        slot.key = key;
        if (auto* nd = factory.node(key, VE_FACTORY_KEY_SEP)) {
            if (auto* pn = nd->find("priority")) slot.priority = pn->getInt(100);
        }
        slots.push_back(std::move(slot));
    }
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
                veLogW << "[ve/entry] Dependency not found: " << slot.key << " -> " << dep_key;
                continue;
            }
            adj[dit->second].push_back(to);
            indegree[to]++;
        }
    }

    // Kahn's topo sort. Input order already encodes hierarchy + priority, so
    // the ready-set tiebreaker is just input index — keeps subtrees contiguous
    // and only lets explicit depends rearrange things.
    auto cmp = [](int a, int b) { return a > b; };
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
        veLogE << "[ve/entry] Circular dependency detected in module graph!";
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
    bool verbose = g.verbose;

    loadPlugins();

    // Select keys, then materialize their nodes in that exact order. Config
    // subtrees are copied in as each node is created.
    Vector<std::string> keys = selectModuleKeys();
    buildModuleNodes(keys);

    buildModuleGraph(g.modules, keys);
    resolveDepends(g.modules);

    auto& factory = module::factory();

    for (auto& slot : g.modules) {
        if (verbose) {
            veLogI << "[ve/entry] Creating module: " << slot.key;
        }
        try {
            const auto& cfactory = factory;
            auto* nd = cfactory.node(slot.key, VE_FACTORY_KEY_SEP);
            slot.instance = (nd && nd->get().isCallable())
                ? static_cast<Module*>(nd->get().invoke().toPointer())
                : nullptr;
            // cache instance on the factory node
            if (slot.instance && nd) {
                nd->at("instance")->set(Var(static_cast<void*>(slot.instance)));
            }
        } catch (const std::exception& e) {
            veLogE << "[ve/entry] Module create failed (" << slot.key << "): " << e.what();
        }
        if (slot.instance && verbose) {
            veLogI << "[ve/entry] Module created: " << slot.key;
        }
    }

    g.state = INIT;
    for (auto& slot : g.modules) {
        if (!slot.instance) continue;
        if (verbose) {
            veLogI << "[ve/entry] INIT: " << slot.key;
        }
        slot.instance->exeState<Module::INIT>();
    }

    if (verbose) {
        veLogI << "[ve/entry] " << g.modules.size() << " modules initialized";
    }

    // prepare(): forward order (parents first, children last)
    for (auto& slot : g.modules) {
        if (!slot.instance) continue;
        if (verbose) {
            veLogI << "[ve/entry] PREPARE: " << slot.key;
        }
        slot.instance->exeState<Module::PREPARE>();
    }

    // ready(): reverse order (children first, parents last)
    for (int i = (int)g.modules.size() - 1; i >= 0; --i) {
        auto& slot = g.modules[i];
        if (!slot.instance) continue;
        if (verbose) {
            veLogI << "[ve/entry] READY: " << slot.key;
        }
        slot.instance->exeState<Module::READY>();
    }

    g.state = READY;

    // Staging area done its job: config has been applied to module subtrees,
    // settings captured on g / factory / module nodes. Drop it so downstream
    // code sees a clean tree.
    node::root()->erase("ve/entry");

    if (verbose) {
        veLogI << "[ve/entry] " << g.modules.size() << " modules ready";
    }
}

// --- run -------------------------------------------------------------------

int run()
{
    G().state = RUNNING;
    Loop* main_loop = loop::main();
    return main_loop ? main_loop->exec() : 0;
}

void requestQuit(int exit_code)
{
    if (Loop* main_loop = loop::main()) main_loop->quit(exit_code);
}

// --- deinit ----------------------------------------------------------------

void deinit()
{
    auto& g = G();
    bool verbose = g.verbose;

    for (int i = (int)g.modules.size() - 1; i >= 0; --i) {
        auto& slot = g.modules[i];
        if (!slot.instance) continue;
        if (verbose) {
            veLogI << "[ve/entry] DEINIT: " << slot.key;
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

    if (verbose) {
        veLogI << "[ve/entry] deinit complete";
    }
}

// --- convenience -----------------------------------------------------------

int exec(int argc, char** argv)
{
    if (!setup(argc, argv)) return 2;
    init();
    int code = run();
    deinit();
    return code;
}

// --- queries ---------------------------------------------------------------

State state() { return G().state; }

std::pair<int, char**> args() { return { G().argc, G().argv }; }
const std::string& appName() { return G().app_name; }
bool   verbose() { return G().verbose; }

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
        veLogE << "[ve/plugin] LoadLibrary failed: " << path
               << " (error " << err << ")";
        return false;
    }
    handle = (void*)mod;
#else
    handle = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        veLogE << "[ve/plugin] dlopen failed: " << path
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
    info.api_version = version::number(name);

    pluginList().push_back(std::move(info));
    veLogI << "[ve/plugin] Loaded: " << path;
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
            veLogI << "[ve/plugin] Unloaded: " << name;
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
