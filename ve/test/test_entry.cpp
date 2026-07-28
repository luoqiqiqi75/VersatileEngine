// test_entry.cpp — ve::entry setup: config file loading, CLI parsing, /ve/entry shape
//
// init() is deliberately not exercised here: it instantiates every registered
// module (ve.core, ve.client, ve.server), which would open real sockets from a
// unit-test binary. These tests cover the rewritten setup() surface only.
#include "ve_test.h"

#include "ve/entry.h"
#include "ve/core/node.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace ve;

namespace {

// A config path that is guaranteed not to exist, so setup() neither loads it
// nor falls back to discovering ve.json in the test's working directory.
const char* kNoFile = "__ve_entry_test_absent.json";

void resetEntry()
{
    node::root()->erase("ve/entry");
}

// Owns the strings argv points into; entry::setup keeps the char** but the
// tests here never read it back after the Argv dies.
struct Argv {
    std::vector<std::string> store;
    std::vector<char*>       ptrs;

    Argv(std::initializer_list<const char*> tokens)
    {
        for (const char* t : tokens) store.emplace_back(t);
        ptrs.reserve(store.size() + 1);
        for (auto& s : store) ptrs.push_back(s.data());
        ptrs.push_back(nullptr);
    }

    int    argc() { return static_cast<int>(store.size()); }
    char** argv() { return ptrs.data(); }
};

struct TempJson {
    std::filesystem::path path;

    TempJson(const std::string& name, const std::string& content)
        : path(std::filesystem::temp_directory_path() / name)
    {
        std::ofstream ofs(path, std::ios::binary);
        ofs << content;
    }
    ~TempJson()
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    std::string str() const { return path.string(); }
};

} // namespace

// ============================================================================
// Config file loading
// ============================================================================

VE_TEST(entry_setup_missing_file_is_not_an_error) {
    resetEntry();
    VE_ASSERT(entry::setup(std::string(kNoFile)));
    VE_ASSERT(node::root()->find("ve/entry") != nullptr);
}

VE_TEST(entry_setup_loads_reserved_keys) {
    resetEntry();
    TempJson cfg("ve_entry_test_basic.json", R"({
        "app": "leo",
        "log": { "level": "warn" },
        "version": 2,
        "blacklist": [ "ve.service.x" ],
        "plugins": [ { "path": "a.dll" } ]
    })");

    VE_ASSERT(entry::setup(cfg.str()));

    Node* e = n("ve/entry");
    VE_ASSERT_EQ(e->get("app").toString(), std::string("leo"));
    VE_ASSERT_EQ(e->get("log/level").toString(), std::string("warn"));
    VE_ASSERT_EQ(e->get("blacklist/#0").toString(), std::string("ve.service.x"));
    VE_ASSERT_EQ(e->get("plugins/#0/path").toString(), std::string("a.dll"));
    VE_ASSERT_EQ(entry::appName(), std::string("leo"));
}

VE_TEST(entry_setup_modules_subtree_is_verbatim) {
    resetEntry();
    TempJson cfg("ve_entry_test_modules.json", R"({
        "version": 2,
        "modules": {
            "ve":  { "server": { "node": { "http": { "config": { "port": 13000 } } } } },
            "leo": { "robot": { "controller": { "backend": "external" } } }
        }
    })");

    VE_ASSERT(entry::setup(cfg.str()));

    Node* m = n("ve/entry/modules");
    VE_ASSERT_EQ(m->get("ve/server/node/http/config/port").toInt(0), 13000);
    VE_ASSERT_EQ(m->get("leo/robot/controller/backend").toString(), std::string("external"));
}

VE_TEST(entry_setup_rejects_newer_config_version) {
    resetEntry();
    TempJson cfg("ve_entry_test_version.json",
                 R"({ "version": 999, "app": "future" })");

    VE_ASSERT(!entry::setup(cfg.str()));
}

VE_TEST(entry_setup_accepts_config_without_version) {
    resetEntry();
    TempJson cfg("ve_entry_test_noversion.json", R"({ "app": "bare" })");

    VE_ASSERT(entry::setup(cfg.str()));
    VE_ASSERT_EQ(n("ve/entry")->get("app").toString(), std::string("bare"));
}

// ============================================================================
// CLI parsing — flags land directly on the node paths they will occupy
// ============================================================================

VE_TEST(entry_cli_verbose) {
    resetEntry();
    Argv a{"ve", "-c", kNoFile, "-v"};
    VE_ASSERT(entry::setup(a.argc(), a.argv()));

    VE_ASSERT(n("ve/entry")->get("verbose").toBool(false));
    VE_ASSERT(entry::verbose());
}

VE_TEST(entry_cli_local_terminal) {
    resetEntry();
    Argv a{"ve", "-c", kNoFile, "-t"};
    VE_ASSERT(entry::setup(a.argc(), a.argv()));

    VE_ASSERT(n("ve/entry")->get("modules/ve/client/terminal/stdio/enabled").toBool(false));
}

VE_TEST(entry_cli_remote_terminal_with_endpoint) {
    resetEntry();
    Argv a{"ve", "-c", kNoFile, "-r", "10.0.0.5:9000"};
    VE_ASSERT(entry::setup(a.argc(), a.argv()));

    Node* tcp = n("ve/entry/modules/ve/client/terminal/tcp");
    VE_ASSERT(tcp->get("enabled").toBool(false));
    VE_ASSERT_EQ(tcp->get("config/host").toString(), std::string("10.0.0.5"));
    VE_ASSERT_EQ(tcp->get("config/port").toInt(0), 9000);
}

VE_TEST(entry_cli_terminal_flags_are_mutually_exclusive) {
    resetEntry();
    Argv a{"ve", "-c", kNoFile, "-t", "-r"};
    VE_ASSERT(!entry::setup(a.argc(), a.argv()));
}

VE_TEST(entry_cli_plugin_positional) {
    resetEntry();
    Argv a{"ve", "-c", kNoFile, "veqt.dll"};
    VE_ASSERT(entry::setup(a.argc(), a.argv()));

    VE_ASSERT_EQ(n("ve/entry")->get("plugins/#0/path").toString(), std::string("veqt.dll"));
}

// ============================================================================
// Unknown flags survive for the application to parse
// ============================================================================

VE_TEST(entry_cli_keeps_unknown_flags_in_argv) {
    resetEntry();
    Argv a{"ve", "-c", kNoFile, "--driver", "body=ros", "--port-offset", "3"};
    VE_ASSERT(entry::setup(a.argc(), a.argv()));

    Node* argv_n = n("ve/entry/argv");
    std::vector<std::string> tokens;
    for (Node* c : argv_n->children()) tokens.push_back(c->getString());

    VE_ASSERT_EQ(tokens.size(), static_cast<std::size_t>(7));
    VE_ASSERT_EQ(tokens[3], std::string("--driver"));
    VE_ASSERT_EQ(tokens[4], std::string("body=ros"));
    VE_ASSERT_EQ(tokens[6], std::string("3"));
}

VE_TEST(entry_cli_unknown_flags_do_not_fail_setup) {
    resetEntry();
    Argv a{"ve", "-c", kNoFile, "--totally-unknown", "--hw", "all=fake"};
    VE_ASSERT(entry::setup(a.argc(), a.argv()));
}

// There is no generic path-override flag: --set is just another unrecognized
// token, kept in argv rather than applied to the tree.
VE_TEST(entry_cli_no_generic_set_flag) {
    resetEntry();
    Argv a{"ve", "-c", kNoFile, "--set=modules/leo/name=robot"};
    VE_ASSERT(entry::setup(a.argc(), a.argv()));

    Node* e = n("ve/entry");
    VE_ASSERT(e->find("modules/leo/name") == nullptr);
    VE_ASSERT_EQ(e->get("argv/#3").toString(),
                 std::string("--set=modules/leo/name=robot"));
}

// ============================================================================
// Precedence: CLI overlays the config file
// ============================================================================

VE_TEST(entry_cli_overrides_config_file) {
    resetEntry();
    TempJson cfg("ve_entry_test_prec.json", R"({
        "version": 2,
        "app": "from_file",
        "verbose": false,
        "modules": { "ve": { "server": { "node": { "http": { "config": { "port": 12000 } } } } } }
    })");

    Argv a{"ve", "-c", "PLACEHOLDER", "-v"};
    a.store[2] = cfg.str();
    a.ptrs[2]  = a.store[2].data();

    VE_ASSERT(entry::setup(a.argc(), a.argv()));

    Node* e = n("ve/entry");
    // Untouched by CLI: the file's value stands.
    VE_ASSERT_EQ(e->get("app").toString(), std::string("from_file"));
    VE_ASSERT_EQ(e->get("modules/ve/server/node/http/config/port").toInt(0), 12000);
    // Set by both: the CLI wins.
    VE_ASSERT(e->get("verbose").toBool(false));
    VE_ASSERT(entry::verbose());
}
