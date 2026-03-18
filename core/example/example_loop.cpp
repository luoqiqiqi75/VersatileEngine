// example_loop.cpp — EventLoop, Object signal dispatch, and loop utilities
//
// Demonstrates:
//   - EventLoop creation, start, post, stop
//   - Object::connect with LoopRef for queued dispatch
//   - loop::main() global event loop
//   - Alive token for guarded dispatch

#include <ve/core/node.h>
#include <iostream>
#include <chrono>

using namespace ve;

static void print(const std::string& title) {
    std::cout << "\n=== " << title << " ===" << std::endl;
}

// ---------------------------------------------------------------------------
// 1. Basic EventLoop usage
// ---------------------------------------------------------------------------
static void demo_basic_loop()
{
    print("Basic EventLoop");

    EventLoop loop("worker", 1);
    loop.start();

    std::atomic<int> counter{0};

    for (int i = 0; i < 5; i++) {
        loop.post([&counter, i] {
            counter++;
            std::cout << "  task " << i << " on worker thread" << std::endl;
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    loop.stop();

    std::cout << "completed tasks: " << counter.load() << std::endl;
}

// ---------------------------------------------------------------------------
// 2. Signal dispatch via LoopRef (queued connection)
// ---------------------------------------------------------------------------
static void demo_signal_with_loop()
{
    print("Signal Dispatch via LoopRef");

    EventLoop loop("dispatch", 1);
    loop.start();

    Node source("sensor");
    Object observer("handler");

    std::atomic<bool> received{false};

    // connect with LoopRef: callback runs on the loop's thread, not the caller's
    source.connect<Node::NODE_CHANGED>(&observer, [&received](Var new_val) {
        std::cout << "  [on loop thread] value changed to: " << new_val.toString() << std::endl;
        received = true;
    }, LoopRef(loop));

    // trigger from main thread
    source.set(Var(42));

    // wait for dispatch
    for (int i = 0; i < 50 && !received; i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    std::cout << "signal delivered: " << (received.load() ? "yes" : "no") << std::endl;

    source.disconnect(&observer);
    loop.stop();
}

// ---------------------------------------------------------------------------
// 3. Alive token — guarded dispatch
// ---------------------------------------------------------------------------
static void demo_alive_guard()
{
    print("Alive Token (Guarded Dispatch)");

    EventLoop loop("guarded", 1);
    loop.start();

    auto token = Alive::create();
    std::atomic<int> executed{0};

    // post with alive guard
    loop.post(token, [&executed] {
        executed++;
        std::cout << "  guarded task 1 executed" << std::endl;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // kill the token — future guarded posts will be discarded
    token.kill();

    loop.post(token, [&executed] {
        executed++;
        std::cout << "  guarded task 2 executed (should NOT appear)" << std::endl;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    loop.stop();

    std::cout << "tasks executed: " << executed.load() << " (expected 1)" << std::endl;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main()
{
    std::cout << "VersatileEngine Core — EventLoop Example" << std::endl;

    demo_basic_loop();
    demo_signal_with_loop();
    demo_alive_guard();

    std::cout << "\nDone." << std::endl;
    return 0;
}
