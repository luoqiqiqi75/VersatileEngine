// example_var.cpp — Var variant type and Convert custom types
//
// Demonstrates:
//   - Var basic types: Bool, Int, Double, String, List, Dict
//   - Var::custom<T>() with Convert<T> for user-defined types
//   - convert::Custom<From,To> extension point
//   - Type introspection and conversion

#include <ve/core/node.h>
#include <iostream>

using namespace ve;

static void print(const std::string& title) {
    std::cout << "\n=== " << title << " ===" << std::endl;
}

// ---------------------------------------------------------------------------
// User-defined type: Point3D
// ---------------------------------------------------------------------------
struct Point3D {
    double x = 0, y = 0, z = 0;
};

// register string conversion: Point3D <-> std::string
// Just specialize convert::Custom and provide apply() — no _not_impl base = detected automatically.
template<> struct ve::convert::Custom<Point3D, std::string> {
    static bool apply(const Point3D& p, std::string& out) {
        out = std::to_string(p.x) + "," + std::to_string(p.y) + "," + std::to_string(p.z);
        return true;
    }
};

template<> struct ve::convert::Custom<std::string, Point3D> {
    static bool apply(const std::string& s, Point3D& out) {
        if (sscanf(s.c_str(), "%lf,%lf,%lf", &out.x, &out.y, &out.z) == 3)
            return true;
        return false;
    }
};

// ---------------------------------------------------------------------------
// 1. Basic Var types
// ---------------------------------------------------------------------------
static void demo_basic_types()
{
    print("Basic Var Types");

    Var null_v;
    Var bool_v(true);
    Var int_v(42);
    Var double_v(3.14);
    Var str_v("hello");

    std::cout << "null:   type=" << (int)null_v.type()   << " isNull=" << null_v.isNull() << std::endl;
    std::cout << "bool:   " << bool_v.toBool()           << std::endl;
    std::cout << "int:    " << int_v.toInt()             << std::endl;
    std::cout << "double: " << double_v.toDouble()       << std::endl;
    std::cout << "string: " << str_v.toString()          << std::endl;

    // implicit numeric conversion
    std::cout << "int as double: " << int_v.toDouble()    << std::endl;
    std::cout << "double as int: " << double_v.toInt()    << std::endl;
    std::cout << "int toString:  " << int_v.toString()    << std::endl;
}

// ---------------------------------------------------------------------------
// 2. List and Dict
// ---------------------------------------------------------------------------
static void demo_list_dict()
{
    print("Var List & Dict");

    // list
    Var::ListV items = { Var(1), Var("two"), Var(3.0) };
    Var list_v(std::move(items));
    std::cout << "list[0]: " << list_v[0].toInt()        << std::endl;
    std::cout << "list[1]: " << list_v[1].toString()     << std::endl;
    std::cout << "list[2]: " << list_v[2].toDouble()     << std::endl;
    std::cout << "list toString: " << list_v.toString()  << std::endl;

    // dict
    Var::DictV map;
    map["name"] = Var("robot");
    map["version"] = Var(2);
    map["active"] = Var(true);
    Var dict_v(std::move(map));
    std::cout << "dict toString: " << dict_v.toString()  << std::endl;

    // access dict entries via toDict()
    const auto& d = dict_v.toDict();
    for (const auto& kv : d)
        std::cout << "  " << kv.first << " = " << kv.second.toString() << std::endl;
}

// ---------------------------------------------------------------------------
// 3. Custom types with Var::custom<T>
// ---------------------------------------------------------------------------
static void demo_custom_type()
{
    print("Custom Type: Point3D");

    Point3D pt{1.5, 2.5, 3.5};

    // store as Var
    Var v = Var::custom(pt);
    std::cout << "type is Custom: " << v.isCustom() << std::endl;
    std::cout << "customIs<Point3D>: " << v.customIs<Point3D>() << std::endl;
    std::cout << "toString: " << v.toString() << std::endl;

    // extract back
    if (auto* p = v.customPtr<Point3D>())
        std::cout << "extracted: (" << p->x << ", " << p->y << ", " << p->z << ")" << std::endl;

    // as<T>() shorthand
    Point3D p2 = v.as<Point3D>();
    std::cout << "as<Point3D>: (" << p2.x << ", " << p2.y << ", " << p2.z << ")" << std::endl;

    // round-trip via string: Var -> toString -> fromString -> Point3D
    std::string s = v.toString();
    Point3D p3;
    if (Convert<Point3D>::fromString(s, p3))
        std::cout << "round-trip: \"" << s << "\" -> (" << p3.x << ", " << p3.y << ", " << p3.z << ")" << std::endl;
}

// ---------------------------------------------------------------------------
// 4. Var in Node (custom types stored in the tree)
// ---------------------------------------------------------------------------
static void demo_var_in_node()
{
    print("Custom Type in Node");

    Node root("world");
    root.ensure("robot/position")->set(Var::custom(Point3D{10.0, 20.0, 0.5}));
    root.ensure("robot/target")->set(Var::custom(Point3D{50.0, 50.0, 0.0}));

    // read back with to<T>() (goes through string intermediate if needed)
    auto pos = root.resolve("robot/position")->value().as<Point3D>();
    std::cout << "position: (" << pos.x << ", " << pos.y << ", " << pos.z << ")" << std::endl;

    // value toString works for custom types
    std::cout << "target: " << root.resolve("robot/target")->value().toString() << std::endl;

    std::cout << "\n" << root.dump() << std::endl;
}

// ---------------------------------------------------------------------------
// 5. Comparison and assignment
// ---------------------------------------------------------------------------
static void demo_comparison()
{
    print("Var Comparison & Assignment");

    Var a(42);
    Var b(42);
    Var c(43);
    Var d("42");

    std::cout << "Var(42) == Var(42): " << (a == b) << std::endl;
    std::cout << "Var(42) == Var(43): " << (a == c) << std::endl;
    std::cout << "Var(42) == Var(\"42\"): " << (a == d) << std::endl;

    // from() re-assigns type
    Var v;
    v.fromInt(100);
    std::cout << "fromInt(100): " << v.toInt() << std::endl;
    v.fromString("now a string");
    std::cout << "fromString: " << v.toString() << std::endl;
    v.from(3.14);
    std::cout << "from(3.14): " << v.toDouble() << std::endl;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main()
{
    std::cout << "VersatileEngine Core — Var & Convert Example" << std::endl;

    demo_basic_types();
    demo_list_dict();
    demo_custom_type();
    demo_var_in_node();
    demo_comparison();

    std::cout << "\nDone." << std::endl;
    return 0;
}
