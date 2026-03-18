// example_command.cpp — Command registration and execution
//
// Demonstrates:
//   - command::reg() with lambda, free function, and member function
//   - command::call() synchronous execution
//   - Multi-step commands with shadow chains
//   - Result inspection (SUCCESS, FAIL, content)
//   - command::keys() for listing registered commands

#include <ve/core/command.h>
#include <iostream>

using namespace ve;

static void print(const std::string& title) {
    std::cout << "\n=== " << title << " ===" << std::endl;
}

// ---------------------------------------------------------------------------
// 1. Basic command registration and calling
// ---------------------------------------------------------------------------
static void demo_basic_command()
{
    print("Basic Commands");

    // simple echo: takes input from Node's value, returns it
    command::reg("echo", [](Node* n) -> Result {
        return Result(Result::SUCCESS, n->value());
    });

    // compute: squares an integer input
    command::reg("square", [](int x) -> Result {
        return Result(Result::SUCCESS, Var(x * x));
    });

    // void return: just prints
    command::reg("greet", [](std::string name) {
        std::cout << "  Hello, " << name << "!" << std::endl;
    });

    // call commands
    auto r1 = command::call("echo", Var("hello world"));
    std::cout << "echo result: " << r1.content().toString() << std::endl;

    auto r2 = command::call("square", Var(7));
    std::cout << "square(7): " << r2.content().toInt() << std::endl;

    std::cout << "greet: ";
    command::call("greet", Var("VersatileEngine"));
}

// ---------------------------------------------------------------------------
// Helper class for member function binding
// ---------------------------------------------------------------------------
class Calculator {
public:
    Result add(Node* n) {
        auto list = n->value().toList();
        if (list.size() < 2) return Result::FAIL;
        double a = list[0].toDouble();
        double b = list[1].toDouble();
        return Result(Result::SUCCESS, Var(a + b));
    }

    void reset() {
        _accumulator = 0;
        std::cout << "  Calculator reset" << std::endl;
    }

    double _accumulator = 0;
};

// ---------------------------------------------------------------------------
// 2. Member function and diverse signatures
// ---------------------------------------------------------------------------
static void demo_member_function()
{
    print("Member Function Commands");

    Calculator calc;

    // bind member function
    command::reg("calc.add", &Calculator::add, &calc);
    command::reg("calc.reset", &Calculator::reset, &calc);

    // call with list input
    auto r = command::call("calc.add", Var(Var::ListV{Var(10.5), Var(20.3)}));
    std::cout << "calc.add(10.5, 20.3) = " << r.content().toDouble() << std::endl;

    std::cout << "calc.reset: ";
    command::call("calc.reset");
}

// ---------------------------------------------------------------------------
// 3. Multi-step command (shadow chain)
// ---------------------------------------------------------------------------
static void demo_multi_step()
{
    print("Multi-Step Command");

    // multi-step: validate -> process -> finalize
    command::reg("pipeline", {
        {"validate", [](Node* n) -> Result {
            int val = n->get<int>();
            if (val < 0) return Result(Result::FAIL, Var("negative input"));
            std::cout << "  [validate] input=" << val << " OK" << std::endl;
            return Result::SUCCESS;
        }},
        {"process", [](Node* n) -> Result {
            int val = n->get<int>();
            int result = val * 2 + 1;
            n->set(Var(result));
            std::cout << "  [process] " << val << " -> " << result << std::endl;
            return Result(Result::SUCCESS, Var(result));
        }},
        {"finalize", [](Node* n) -> Result {
            std::cout << "  [finalize] done, result=" << n->get<int>() << std::endl;
            return Result(Result::SUCCESS, n->value());
        }},
    });

    auto r = command::call("pipeline", Var(10));
    std::cout << "pipeline result: " << (r.isSuccess() ? "SUCCESS" : "FAIL")
              << " content=" << r.content().toString() << std::endl;

    // test failure case
    auto r2 = command::call("pipeline", Var(-5));
    std::cout << "pipeline(-5): " << (r2.isError() ? "FAIL" : "SUCCESS")
              << " content=" << r2.content().toString() << std::endl;
}

// ---------------------------------------------------------------------------
// 4. Query registered commands
// ---------------------------------------------------------------------------
static void demo_query()
{
    print("Registered Commands");

    auto keys = command::keys();
    for (const auto& k : keys)
        std::cout << "  " << k << std::endl;

    std::cout << "has 'echo': " << command::has("echo") << std::endl;
    std::cout << "has 'nope': " << command::has("nope") << std::endl;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main()
{
    std::cout << "VersatileEngine Core — Command Example" << std::endl;

    demo_basic_command();
    demo_member_function();
    demo_multi_step();
    demo_query();

    std::cout << "\nDone." << std::endl;
    return 0;
}
