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
    g.verbose = false;

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
    bool verbose_overridden = false;
    if (options_n) {
        config_file = options_n->get("config_file").toString();
        verbose_overridden = options_n->find("verbose") != nullptr;
        verbose = options_n->get("verbose").toBool(false);
        g.app_name = options_n->get("app").toString(g.app_name);
    }
    g.verbose = verbose;

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
            if (verbose) veLogW << "[ve/entry] Config file empty or missing: " << config_file;
        } else if (!schema::toNode<schema::JsonS>(entry_n, content)) {
            veLogE << "[ve/entry] Config parse failed: " << config_file;
            return false;
        } else {
            if (!verbose_overridden) {
                verbose = entry_n->get("verbose").toBool(false);
                g.verbose = verbose;
            }
            if (verbose) veLogI << "[ve/entry] Config loaded: " << config_file;
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

void init()
{
    auto& g = G();
    const bool verbose = g.verbose;
    Node* entry_n = n("ve/entry");

    // Load configured plugins in declaration order.
    if (Node* plugins = entry_n->find("plugins")) {
        auto load_plugin = [verbose](Node* spec) {
            Node* path_node = spec->find("path");
            if (!path_node || !spec->get("enabled").toBool(true)) return;

            std::string path = path_node->getString();
            if (path.empty() || !plugin::load(path, verbose)) return;

            int min_api = spec->get("min_api").toInt(0);
            if (min_api <= VE_MIN_API) return;

            std::string name = spec->get("name").toString();
            if (name.empty()) name = path;
            if (!version::check(name, min_api)) {
                veLogE << "[ve/entry] Plugin " << name
                       << " version check failed (min_api=" << min_api << ")";
            }
        };

        if (plugins->find("path")) {
            load_plugin(plugins);
        } else {
            for (Node* spec : plugins->children()) load_plugin(spec);
        }
    }

    auto& module_factory = module::factory();
    Node* modules_config = entry_n->at("modules");

    Vector<std::string> blacklist;
    if (Node* black = entry_n->find("blacklist")) {
        for (Node* item : black->children()) blacklist.push_back(item->getString());
    }

    struct Candidate {
        std::string key;
        int priority = 100;
    };

    // Filter registered modules and bucket them by their registered parent.
    Hash<Vector<Candidate>> module_tree;
    Hash<int> selected;
    for (const auto& key : factory::keys("module")) {
        bool blocked = false;
        for (const auto& item : blacklist) {
            if (item.empty()) continue;
            if (key == item ||
                (key.size() > item.size() && key.compare(0, item.size(), item) == 0 && key[item.size()] == '.')) {
                blocked = true;
                break;
            }
        }
        if (blocked) continue;

        // Service modules are opt-in and need a matching config subtree.
        if (key.rfind("ve.service.", 0) == 0 && !modules_config->find(keyToPath(key))) {
            continue;
        }

        int priority = 100;
        if (Node* factory_node = module_factory.node(key, VE_FACTORY_KEY_SEP)) {
            priority = factory_node->get("priority").toInt(100);
        }
        selected[key] = 1;
        module_tree[parentKey(key)].push_back(Candidate{key, priority});
    }

    // Attach orphaned subtrees to their nearest selected ancestor.
    Hash<Vector<Candidate>> ordered_tree;
    for (auto it = module_tree.begin(); it != module_tree.end(); ++it) {
        std::string parent = it->first;
        while (!parent.empty() && selected.find(parent) == selected.end()) {
            parent = parentKey(parent);
        }
        auto& bucket = ordered_tree[parent];
        for (auto& candidate : it->second) bucket.push_back(std::move(candidate));
    }
    for (auto it = ordered_tree.begin(); it != ordered_tree.end(); ++it) {
        std::stable_sort(it->second.begin(), it->second.end(),
            [](const Candidate& a, const Candidate& b) { return a.priority < b.priority; });
    }

    Vector<std::string> keys;
    keys.reserve(selected.size());
    std::function<void(const std::string&)> append_children = [&](const std::string& parent) {
        auto it = ordered_tree.find(parent);
        if (it == ordered_tree.end()) return;
        for (const auto& candidate : it->second) {
            keys.push_back(candidate.key);
            append_children(candidate.key);
        }
    };
    append_children("");

    // Materialize nodes and slots in the selected hierarchy/priority order.
    g.modules.clear();
    g.modules.reserve(keys.size());
    Node* root = node::root();
    for (const auto& key : keys) {
        std::string path = keyToPath(key);
        Node* module_node = root->atPath(path);
        if (Node* config = modules_config->find(path)) module_node->copy(config);

        ModuleSlot slot;
        slot.key = key;
        if (Node* factory_node = module_factory.node(key, VE_FACTORY_KEY_SEP)) {
            slot.priority = factory_node->get("priority").toInt(100);
        }
        g.modules.push_back(std::move(slot));
    }

    // Apply implicit parent dependencies and explicit configured dependencies.
    Hash<int> key_to_index;
    for (int i = 0; i < static_cast<int>(g.modules.size()); ++i) {
        key_to_index[g.modules[i].key] = i;
    }

    const int module_count = static_cast<int>(g.modules.size());
    Vector<Vector<int>> edges(module_count);
    Vector<int> indegree(module_count, 0);
    for (int i = 0; i < module_count; ++i) {
        std::string parent = parentKey(g.modules[i].key);
        while (!parent.empty()) {
            auto parent_it = key_to_index.find(parent);
            if (parent_it != key_to_index.end()) {
                edges[parent_it->second].push_back(i);
                ++indegree[i];
                break;
            }
            parent = parentKey(parent);
        }
    }

    for (int i = 0; i < module_count; ++i) {
        Node* module_node = root->find(keyToPath(g.modules[i].key));
        Node* dependencies = module_node ? module_node->find("depends") : nullptr;
        if (!dependencies) continue;

        for (Node* dependency : *dependencies) {
            std::string dependency_key = dependency->getString();
            auto dependency_it = key_to_index.find(dependency_key);
            if (dependency_it == key_to_index.end()) {
                if (verbose) {
                    veLogW << "[ve/entry] Dependency not found: "
                           << g.modules[i].key << " -> " << dependency_key;
                }
                continue;
            }
            edges[dependency_it->second].push_back(i);
            ++indegree[i];
        }
    }

    // Stable topological order: the ready queue uses the hierarchy/priority
    // index as its tiebreaker, so dependencies only move what they must.
    auto later_index = [](int a, int b) { return a > b; };
    std::priority_queue<int, std::vector<int>, decltype(later_index)> ready(later_index);
    for (int i = 0; i < module_count; ++i) {
        if (indegree[i] == 0) ready.push(i);
    }

    Vector<int> order;
    order.reserve(module_count);
    while (!ready.empty()) {
        int current = ready.top();
        ready.pop();
        order.push_back(current);
        for (int next : edges[current]) {
            if (--indegree[next] == 0) ready.push(next);
        }
    }

    if (static_cast<int>(order.size()) == module_count) {
        Vector<ModuleSlot> sorted;
        sorted.reserve(module_count);
        for (int index : order) sorted.push_back(std::move(g.modules[index]));
        g.modules = std::move(sorted);
    } else {
        veLogE << "[ve/entry] Circular dependency detected in module graph!";
    }

    // Create modules, then drive their lifecycle directly.
    for (auto& slot : g.modules) {
        if (verbose) veLogI << "[ve/entry] Creating module: " << slot.key;
        try {
            const auto& const_factory = module_factory;
            Node* factory_node = const_factory.node(slot.key, VE_FACTORY_KEY_SEP);
            slot.instance = (factory_node && factory_node->get().isCallable())
                ? static_cast<Module*>(factory_node->get().invoke().toPointer())
                : nullptr;
            if (slot.instance && factory_node) {
                factory_node->at("instance")->set(Var(static_cast<void*>(slot.instance)));
            }
        } catch (const std::exception& e) {
            veLogE << "[ve/entry] Module create failed (" << slot.key << "): " << e.what();
        }
        if (slot.instance && verbose) veLogI << "[ve/entry] Module created: " << slot.key;
    }

    g.state = INIT;
    for (auto& slot : g.modules) {
        if (!slot.instance) continue;
        if (verbose) veLogI << "[ve/entry] INIT: " << slot.key;
        slot.instance->exeState<Module::INIT>();
    }
    if (verbose) veLogI << "[ve/entry] " << g.modules.size() << " modules initialized";

    // prepare(): forward order (parents first, children last)
    for (auto& slot : g.modules) {
        if (!slot.instance) continue;
        if (verbose) veLogI << "[ve/entry] PREPARE: " << slot.key;
        slot.instance->exeState<Module::PREPARE>();
    }

    // ready(): reverse order (children first, parents last)
    for (int i = static_cast<int>(g.modules.size()) - 1; i >= 0; --i) {
        auto& slot = g.modules[i];
        if (!slot.instance) continue;
        if (verbose) veLogI << "[ve/entry] READY: " << slot.key;
        slot.instance->exeState<Module::READY>();
    }

    g.state = READY;

    // Configuration has been applied; remove the staging area.
    root->erase("ve/entry");
    if (verbose) veLogI << "[ve/entry] " << g.modules.size() << " modules ready";
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

bool load(const std::string& path, bool verbose)
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
    if (verbose) veLogI << "[ve/plugin] Loaded: " << path;
    return true;
}

bool unload(const std::string& name, bool verbose)
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
            if (verbose) veLogI << "[ve/plugin] Unloaded: " << name;
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
