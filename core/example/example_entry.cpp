// example_entry.cpp  - ve::entry module system demo
//
// Demonstrates:
//   - JSON config loading
//   - Built-in module lifecycle (ve.core, ve.service.terminal)
//   - Custom module registration with priority & version
//   - Node tree inspection after init
//
// Run:
//   example_entry              (loads ve.json from current dir)
//   example_entry my_conf.json (loads custom config)
//   example_entry --verbose    (force verbose without config)

#include <ve/entry.h>
#include <ve/core/log.h>
#include <ve/core/node.h>
#include <ve/core/command.h>
#include <iostream>
#include <thread>
#include <chrono>

using namespace ve;

// ============================================================================
// A custom module  - defined entirely in one file, no header needed
// ============================================================================

class DemoModule : public Module
{
public:
    explicit DemoModule(const std::string& name) : Module(name)
    {
        veLogI << "[demo] constructor  - reading config from node()";

        std::string greeting = "Hello VE!";
        if (auto* gn = node()->resolve("config/greeting"))
            greeting = gn->get<std::string>(greeting);

        n("app/greeting")->set(Var(greeting));
        veLogI << "[demo] greeting = " << greeting;
    }

    ~DemoModule()
    {
        veLogI << "[demo] destructor  - cleanup";
    }

protected:
    void init() override
    {
        veLogI << "[demo] init  - registering commands & data";

        n("app/counter")->set(Var(0));

        command::reg("demo.inc", [](const Var&) -> Result {
            auto* cn = ve::n("app/counter");
            int v = cn->get<int>() + 1;
            cn->set(Var(v));
            return Result(Result::SUCCESS, Var(v));
        });

        command::reg("demo.greet", [](const Var&) -> Result {
            std::string g = ve::n("app/greeting")->get<std::string>();
            return Result(Result::SUCCESS, Var(g));
        });
    }

    void ready() override
    {
        veLogI << "[demo] ready  - cross-module wiring done";
    }

    void deinit() override
    {
        veLogI << "[demo] deinit  - releasing resources";
    }
};

VE_REGISTER_PRIORITY_MODULE(demo, DemoModule, 80, 1)

// ============================================================================
// main
// ============================================================================

static void printNodeTree()
{
    veLogI << "--- Node tree snapshot ---";
    veLogI << node::root()->dump();
    veLogI << "--- end ---";
}

int main(int argc, char* argv[])
{
    std::cout << "=== VersatileEngine Entry Example ===" << std::endl;
    std::cout << std::endl;

    // --- Step-by-step usage ---

    // 1) setup: load JSON config → ve/entry/ node tree
    if (argc > 1 && argv[1][0] != '-')
        entry::setup(argv[1]);
    else
        entry::setup("ve.json");

    // 2) init: create modules (by priority) → init → ready
    entry::init();

    // 3) inspect
    printNodeTree();

    // Run a command
    auto r = command::call("demo.inc");
    veLogI << "[main] demo.inc returned: " << r.content().to<int>();
    r = command::call("demo.inc");
    veLogI << "[main] demo.inc returned: " << r.content().to<int>();
    r = command::call("demo.greet");
    veLogI << "[main] demo.greet returned: " << r.content().to<std::string>();

    // Check version
    veLogI << "[main] demo version = " << version::number("demo");
    veLogI << "[main] ve.core version = " << version::number("ve.core");

    std::cout << std::endl;
    std::cout << "Terminal is running - try: telnet localhost 5061" << std::endl;
    std::cout << "Press Enter to quit..." << std::endl;
    std::cin.get();

    // 4) deinit: deinit modules (reverse) → destroy
    entry::deinit();

    std::cout << "Bye!" << std::endl;
    return 0;
}
