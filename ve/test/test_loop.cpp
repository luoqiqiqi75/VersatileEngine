// ----------------------------------------------------------------------------
// test_loop.cpp — ve::Loop / EventLoop / LoopRef
// ----------------------------------------------------------------------------

#include "ve_test.h"
#include "ve/core/loop.h"
#include "ve/core/object.h"
#include <atomic>
#include <chrono>
#include <thread>

using namespace ve;

// --- basic lifecycle ---

VE_TEST(loop_create_destroy) {
    EventLoop loop("test");
    VE_ASSERT_EQ(loop.name(), "test");
    VE_ASSERT(!loop.isRunning());
}

VE_TEST(loop_start_stop) {
    EventLoop loop("test", 1);
    VE_ASSERT(loop.start());
    VE_ASSERT(loop.isRunning());
    VE_ASSERT(loop.stop());
    VE_ASSERT(!loop.isRunning());
}

VE_TEST(loop_double_start) {
    EventLoop loop("test", 1);
    VE_ASSERT(loop.start());
    VE_ASSERT(!loop.start());  // already running
    loop.stop();
}

VE_TEST(loop_double_stop) {
    EventLoop loop("test", 1);
    loop.start();
    VE_ASSERT(loop.stop());
    VE_ASSERT(!loop.stop());   // already stopped
}

// --- post ---

VE_TEST(loop_post_single) {
    EventLoop loop("test", 1);
    loop.start();

    std::atomic<int> count{0};
    loop.post([&] { count.store(42); });

    // wait for task to complete
    for (int i = 0; i < 100 && count.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    VE_ASSERT_EQ(count.load(), 42);
    loop.stop();
}

VE_TEST(loop_post_multiple) {
    EventLoop loop("test", 2);
    loop.start();

    std::atomic<int> count{0};
    for (int i = 0; i < 100; ++i)
        loop.post([&] { count.fetch_add(1); });

    // wait for tasks
    for (int i = 0; i < 200 && count.load() < 100; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    VE_ASSERT_EQ(count.load(), 100);
    loop.stop();
}

VE_TEST(loop_post_from_threads) {
    EventLoop loop("test", 2);
    loop.start();

    std::atomic<int> count{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < 250; ++i)
                loop.post([&] { count.fetch_add(1); });
        });
    }
    for (auto& th : threads) th.join();

    // wait for all tasks
    for (int i = 0; i < 500 && count.load() < 1000; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    VE_ASSERT_EQ(count.load(), 1000);
    loop.stop();
}

// --- LoopRef ---

VE_TEST(loop_ref_from_loop) {
    EventLoop loop("test", 1);
    loop.start();

    LoopRef ref = loop;
    VE_ASSERT(!!ref);

    std::atomic<int> val{0};
    ref.post([&] { val.store(99); });

    for (int i = 0; i < 100 && val.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    VE_ASSERT_EQ(val.load(), 99);
    loop.stop();
}

VE_TEST(loop_ref_empty) {
    LoopRef ref;
    VE_ASSERT(!ref);
    ref.post([] {});  // should not crash
}

VE_TEST(loop_ref_dead_loop) {
    LoopRef ref;
    {
        EventLoop loop("temp", 1);
        loop.start();
        ref = loop;
        VE_ASSERT(!!ref);
    }  // loop destroyed here
    // ref still "looks" valid (first non-empty) but loop is dead
    ref.post([] {});  // should not crash — alive check prevents call
}

// --- global loop ---

VE_TEST(loop_global_main) {
    auto& m = loop::main();
    VE_ASSERT(m.isRunning());
    VE_ASSERT_EQ(m.name(), "ve.loop.main");

    std::atomic<int> val{0};
    m.post([&] { val.store(1); });

    for (int i = 0; i < 100 && val.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    VE_ASSERT_EQ(val.load(), 1);
}

VE_TEST(loop_global_pool) {
    auto& p = loop::pool();
    VE_ASSERT(p.isRunning());
    VE_ASSERT_EQ(p.name(), "ve.loop.pool");
}

VE_TEST(loop_convenience_post) {
    std::atomic<int> val{0};
    loop::post([&] { val.store(77); });

    for (int i = 0; i < 100 && val.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    VE_ASSERT_EQ(val.load(), 77);
}

// --- restart ---

VE_TEST(loop_restart) {
    EventLoop loop("test", 1);
    loop.start();

    std::atomic<int> val{0};
    loop.post([&] { val.store(1); });
    for (int i = 0; i < 100 && val.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    VE_ASSERT_EQ(val.load(), 1);

    loop.stop();

    // restart
    loop.start();
    loop.post([&] { val.store(2); });
    for (int i = 0; i < 100 && val.load() == 1; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    VE_ASSERT_EQ(val.load(), 2);

    loop.stop();
}

// --- AliveToken: owner destroyed → task discarded ---

VE_TEST(loop_alive_token_basic) {
    auto token = Alive::create();
    EventLoop loop("test", 1);
    loop.start();

    std::atomic<int> val{0};
    loop.post(token, [&] { val.store(1); });

    for (int i = 0; i < 100 && val.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    VE_ASSERT_EQ(val.load(), 1);
    loop.stop();
}

VE_TEST(loop_alive_token_dead) {
    auto token = Alive::create();
    EventLoop loop("test", 1);
    loop.start();

    std::atomic<int> val{0};
    token.kill();  // "dead" before task executes
    loop.post(token, [&] { val.store(99); });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    VE_ASSERT_EQ(val.load(), 0);  // task was discarded
    loop.stop();
}

VE_TEST(loop_post_token_alive) {
    auto token = Alive::create();
    std::atomic<int> val{0};

    loop::post(token, [&] { val.store(42); });

    for (int i = 0; i < 100 && val.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    VE_ASSERT_EQ(val.load(), 42);
}

VE_TEST(loop_post_token_dead) {
    auto token = Alive::create();
    EventLoop loop("test", 1);
    std::atomic<int> val{0};

    loop::post(LoopRef::from(loop), token, [&] { val.store(99); });
    token.kill();  // "dead" before task executes

    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    VE_ASSERT_EQ(val.load(), 0);  // task discarded
    loop.stop();
}

VE_TEST(loop_post_token_context) {
    auto token = Alive::create();
    int dummy;
    void* ctx = &dummy;
    std::atomic<void*> captured_ctx{nullptr};

    loop::post(token, ctx, [&] {
        captured_ctx.store(loop::context());
    });

    for (int i = 0; i < 100 && !captured_ctx.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    VE_ASSERT_EQ(captured_ctx.load(), ctx);
}

VE_TEST(loop_context_null_outside) {
    VE_ASSERT(loop::context() == nullptr);
}

// --- signal dispatch with loop: observer destroyed → no crash ---

VE_TEST(loop_signal_observer_destroyed) {
    EventLoop loop("test", 1);
    loop.start();

    Object sender("sender");
    auto* observer = new Object("observer");

    std::atomic<int> val{0};
    sender.connect<1>(observer, [&]() { val.store(1); }, LoopRef::from(loop));

    sender.trigger<1>();
    for (int i = 0; i < 100 && val.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    VE_ASSERT_EQ(val.load(), 1);

    // destroy observer → task silently discarded
    delete observer;
    val.store(0);
    sender.trigger<1>();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    VE_ASSERT_EQ(val.load(), 0);

    loop.stop();
}

VE_TEST(loop_signal_sender_destroyed) {
    EventLoop loop("test", 1);

    auto* sender = new Object("sender");
    Object observer("observer");

    std::atomic<int> val{0};
    sender->connect<1>(&observer, [&]() { val.store(1); }, LoopRef::from(loop));

    // queue task while loop is stopped, then destroy sender before starting
    sender->trigger<1>();
    delete sender;

    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    VE_ASSERT_EQ(val.load(), 0);  // task discarded: sender_alive is false
    loop.stop();
}
