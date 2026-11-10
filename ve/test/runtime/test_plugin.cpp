#include "ve_test.h"
#include "ve/entry.h"
#include "ve/core/plugin.h"
#include "ve/core/command.h"
#include "ve/core/schema.h"
#include "ve/core/res.h"
#include "../../src/service/cmd_commands.h"
#include "../../src/service/terminal_session.h"

using namespace ve;

VE_REGISTER_VERSION(ve_plugin_fixture, 7)
VE_REGISTER_VERSION(custom_plugin, 5)

namespace {
const std::string fixture_path = VE_PLUGIN_FIXTURE_PATH;

bool unload(const std::string& name)
{
    Node spec;
    spec.set("name", name);
    return plugin::unload(&spec);
}

struct Runtime
{
    explicit Runtime(Node* overrides = nullptr)
    {
        node::root()->erase("ve/entry");
        Node options;
        options.set("config_file", "__ve_plugin_absent.json");
        options.set("log/console/enabled", false);
        options.set("log/file/enabled", false);
        options.at("blacklist")->append()->set("ve");
        options.copy(overrides);
        VE_ASSERT(entry::setup(&options));
        entry::init();
        static bool commands_installed = false;
        if (!commands_installed) {
            schema::toNode<schema::JsonS>(command::factory().node(), std::string(res::read("ve/service/cmd.json")));
            service::registerCmdCommands(command::factory());
            commands_installed = true;
        }
    }
    ~Runtime()
    {
        auto loaded = plugin::loaded();
        for (auto it = loaded.rbegin(); it != loaded.rend(); ++it) VE_ASSERT(unload(it->name));
        entry::deinit();
    }
};
}

VE_TEST(plugin_single_spec_defaults_and_platform_unload) {
    Runtime runtime;
    Node spec, out;
    spec.set("path", fixture_path);
    VE_ASSERT(plugin::load(&spec, &out));
    VE_ASSERT(n("test/plugin_loaded")->getBool());
    VE_ASSERT_EQ(out.get("plugins/#0/name").toString(), std::string("ve_plugin_fixture"));
    VE_ASSERT_EQ(out.get("count").toInt(), 1);
    VE_ASSERT_EQ(plugin::loaded()[0].api_version, 7);
    VE_ASSERT(!plugin::load(&spec));
    VE_ASSERT(unload("ve_plugin_fixture"));
    VE_ASSERT(!n("test/plugin_loaded")->getBool());
    VE_ASSERT(plugin::loaded().empty());
}

VE_TEST(plugin_entry_and_commands_share_single_and_list_specs) {
    Node options;
    options.at("plugins")->append()->set("path", fixture_path);
    Runtime runtime(&options);
    VE_ASSERT(n("test/plugin_loaded")->getBool());
    VE_ASSERT(unload("ve_plugin_fixture"));
    Node specs, out;
    specs.append()->set("path", fixture_path);
    auto* skipped = specs.append();
    skipped->set("path", "libmissing.dll");
    skipped->set("enabled", false);
    VE_ASSERT(command::create("plugin.load", nullptr, &specs, &out).run().result());
    VE_ASSERT_EQ(out.get("count").toInt(), 1);
    VE_ASSERT(out.get("plugins/#1/skipped").toBool());
    Node paths;
    paths.append()->set("path", fixture_path);
    VE_ASSERT(command::create("plugin.unload", nullptr, &paths).run().result());
    VE_ASSERT(!n("test/plugin_loaded")->getBool());
}

VE_TEST(plugin_name_override_version_and_load_errors) {
    Runtime runtime;
    Node spec;
    spec.set("path", fixture_path);
    spec.set("min_api", 8);
    std::string error;
    VE_ASSERT(!plugin::load(&spec, nullptr, false, &error));
    VE_ASSERT(error.find("version") != std::string::npos);
    VE_ASSERT(plugin::loaded().empty());
    VE_ASSERT(!n("test/plugin_loaded")->getBool());
    spec.set("name", "custom_plugin");
    spec.set("min_api", 5);
    VE_ASSERT(plugin::load(&spec));
    VE_ASSERT_EQ(plugin::loaded()[0].name, std::string("custom_plugin"));
    VE_ASSERT(unload("custom_plugin"));
    spec.set("path", "__ve_missing_plugin.dll");
    VE_ASSERT(!plugin::load(&spec));
    VE_ASSERT(!unload("not_loaded"));
}

VE_TEST(plugin_commands_follow_schema_and_registration_toggle) {
    Runtime runtime;
    VE_ASSERT(command::factory().has("plugin.load"));
    VE_ASSERT(!command::factory().has("module.load"));
    Loop* previous = loop::current();
    loop::setCurrent(loop::main());
    service::TerminalSession session(node::root());
    session.execute("plugin load \"" + fixture_path + "\" --min_api 7");
    VE_ASSERT(n("test/plugin_loaded")->getBool());
    session.execute("plugin unload ve_plugin_fixture");
    VE_ASSERT(plugin::loaded().empty());
    loop::setCurrent(previous);
    Node input;
    input.at("args")->append()->set(fixture_path);
    VE_ASSERT(command::create("plugin.load", nullptr, &input).run().result());
    VE_ASSERT(unload("ve_plugin_fixture"));
    n("ve/entry")->set("plugin_commands", false);
    Factory disabled("test/disabled_plugin_commands");
    service::registerCmdCommands(disabled);
    VE_ASSERT(!disabled.has("plugin.load"));
    VE_ASSERT(!disabled.has("plugin.unload"));
}

VE_TEST(plugin_empty_lists_disabled_specs_and_missing_fields) {
    Runtime runtime;
    Node empty;
    VE_ASSERT(plugin::load(&empty));
    empty.set(Var::ListV{});
    VE_ASSERT(plugin::load(&empty));
    VE_ASSERT(plugin::unload(&empty));
    Node disabled;
    disabled.set("enabled", false);
    VE_ASSERT(plugin::load(&disabled));
    Node invalid;
    invalid.set("name", "missing_path");
    VE_ASSERT(!plugin::load(&invalid));
    VE_ASSERT(plugin::loaded().empty());
}
