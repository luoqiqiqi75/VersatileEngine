// example_node.cpp — Node tree, values, paths, signals, and schema serialization
//
// Demonstrates:
//   - Building a Node tree (append, ensure, insert)
//   - Setting/getting values with Var
//   - Path-based navigation and resolution
//   - Signal/slot: NODE_CHANGED, NODE_ADDED, NODE_ACTIVATED (bubbling)
//   - Schema-based JSON export/import
//   - dump() for tree inspection

#include <ve/core/node.h>
#include <ve/core/command.h>
#include <iostream>

using namespace ve;

static void print(const std::string& title) {
    std::cout << "\n=== " << title << " ===" << std::endl;
}

// ---------------------------------------------------------------------------
// 1. Tree construction and value access
// ---------------------------------------------------------------------------
static void demo_tree_and_values()
{
    print("Tree Construction & Values");

    Node root("robot");

    // append children
    Node* arm = root.append("arm");

    Node* joint1 = arm->append("joint1");
    joint1->set(45.0);

    // Node* joint2 = arm->append("joint2");
    arm->set("joint2", 90.0);

    Node* gripper = arm->append("gripper");
    gripper->set("open");

    // get values
    std::cout << "joint1 angle: " << joint1->get<double>() << root.get("default_str") << std::endl;
    std::cout << "joint2 angle: " << root.resolve("arm/joint2")->get<double>() << std::endl;
    std::cout << "gripper:      " << gripper->get<std::string>() << std::endl;

    // ensure creates the full path if missing
    root.ensure("arm/wrist/roll")->set(15.0);
    root.ensure("arm/wrist/pitch")->set(-5.0);

    // resolve to read back
    if (auto* roll = root.resolve("arm/wrist/roll"))
        std::cout << "wrist roll:   " << roll->get<double>() << std::endl;

    // dump the tree
    std::cout << "\n" << root.dump() << std::endl;
}

// ---------------------------------------------------------------------------
// 2. Path navigation: key format (name, name#N, #N)
// ---------------------------------------------------------------------------
static void demo_path_navigation()
{
    print("Path Navigation");

    Node root("sensors");

    // overlapping names: multiple children named "temp"
    // root.append("temp", 0)->set(Var(36.5));
    // root.append("temp", 1)->set(Var(37.0));
    root.set("temp", 2, 38.2);
    root.append("temp", 1); // append 2 more
    root.append("pressure")->set(1013.25);

    // access by name#N (overlap index)
    std::cout << "temp#0: " << root.resolve("temp#0")->get<double>() << std::endl;
    std::cout << "temp#1: " << root.resolve("temp#1")->get<double>() << std::endl;
    std::cout << "temp#2: " << root.resolve("temp#2")->get<double>() << std::endl;

    // access by global index (#N)
    std::cout << "#3 (pressure): " << root.resolve("#3")->get<double>() << std::endl;

    // iterate children
    std::cout << "\nAll children:" << std::endl;
    for (auto* child : root)
        std::cout << "  " << child->name() << " = " << child->value().toString() << std::endl;

    // child count
    std::cout << "total: " << root.count() << ", temp count: " << root.count("temp") << std::endl;
}

// ---------------------------------------------------------------------------
// 3. Signals: NODE_CHANGED, NODE_ADDED, NODE_REMOVED, NODE_ACTIVATED
// ---------------------------------------------------------------------------
static void demo_signals()
{
    print("Signals");

    Node root("config");
    Object observer("logger");

    // NODE_CHANGED: fired when set() changes a value
    root.connect<Node::NODE_CHANGED>(&observer, [](Var new_val, Var old_val) {
        std::cout << "[CHANGED] " << loop::context() << " " << old_val.toString() << " -> " << new_val.toString() << std::endl;
    });

    std::cout << "update(1) root " << &root << std::endl;
    root.set(1);
    root.set(2);

    // update() only fires if value differs
    std::cout << "update(2) same value -> no signal: " << (!root.update(2) ? "skipped" : "fired") << std::endl;
    std::cout << "update(3) new value:  ";
    root.update(Var(3));

    // NODE_ADDED / NODE_REMOVED
    root.connect<Node::NODE_ADDED>(&observer, [](std::string key, int overlap) {
        std::cout << "[ADDED] key=" << key << " overlap=" << overlap << std::endl;
    });
    root.connect<Node::NODE_REMOVED>(&observer, [](std::string key, int overlap) {
        std::cout << "[REMOVED] key=" << key << " overlap=" << overlap << std::endl;
    });

    root.append("item_a");
    root.append("item_b");
    root.remove("item_a");

    // NODE_ACTIVATED: bubbling
    print("Bubbling (NODE_ACTIVATED)");

    Node grandparent("gp");
    Node* parent = grandparent.append("parent");
    Node* child  = parent->append("child");

    grandparent.watch(true);  // enable bubble reception
    parent->watch(true);

    grandparent.connect<Node::NODE_ACTIVATED>(&observer, [](int signal, Node* source) {
        std::cout << "[BUBBLE@gp] signal=0x" << std::hex << signal << std::dec
                  << " from=" << source->name() << std::endl;
    });

    child->set(Var("hello"));
    child->activate(Node::NODE_CHANGED, child);

    root.disconnect(&observer);
    grandparent.disconnect(&observer);
}

// ---------------------------------------------------------------------------
// 4. Schema + JSON serialization
// ---------------------------------------------------------------------------
static void demo_schema_json()
{
    print("Schema + JSON");

    // define schema
    auto wristSchema = Schema::create({SchemaField("roll"), SchemaField("pitch"), SchemaField("yaw")});
    auto armSchema = Schema::create({
        SchemaField("joint1"),
        SchemaField("joint2"),
        SchemaField("wrist", wristSchema)
    });

    // build tree from schema
    Node arm("arm");
    armSchema->build(&arm);

    // populate values
    arm.set("joint1", 45.0);
    arm.set("joint2", 90.0);
    arm.set("wrist/roll", 10.0);
    arm.set("wrist/pitch", -5.0);
    arm.set("wrist/yaw", 0.0);

    // export to JSON
    auto json = schema::exportAs<schema::Json>(&arm, 2);
    std::cout << "JSON export:\n" << json << std::endl;

    // import into a fresh tree
    Node arm2("arm2");
    armSchema->build(&arm2);
    schema::importAs<schema::Json>(&arm2, json);
    std::cout << "\nImported tree:\n" << arm2.dump() << std::endl;
}

// ---------------------------------------------------------------------------
// 5. Global data tree: ve::n() and ve::d()
// ---------------------------------------------------------------------------
static void demo_global_tree()
{
    print("Global Data Tree");

    // ve::n() uses slash path, creates nodes in the global root
    n("app/version")->set(Var("2.0.0"));
    n("app/debug")->set(Var(true));
    n("device/serial")->set(Var("SN-12345"));

    // ve::d() uses dot path (Data-compatible)
    d("device.status")->set(Var("online"));

    // read back
    std::cout << "app/version:   " << n("app/version")->get<std::string>() << std::endl;
    std::cout << "app/debug:     " << n("app/debug")->get<bool>() << std::endl;
    std::cout << "device.serial: " << d("device.serial")->get<std::string>() << std::endl;
    std::cout << "device.status: " << d("device.status")->get<std::string>() << std::endl;

    // dump the global root
    std::cout << "\nGlobal tree:\n" << node::root()->dump() << std::endl;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main()
{
    std::cout << "VersatileEngine Core — Node Example" << std::endl;

    demo_tree_and_values();
    demo_path_navigation();
    demo_signals();
    demo_schema_json();
    demo_global_tree();

    std::cout << "\nDone." << std::endl;
    return 0;
}
