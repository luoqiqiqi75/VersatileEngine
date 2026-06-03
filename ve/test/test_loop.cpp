// ----------------------------------------------------------------------------
// test_loop.cpp — ve::Loop (non-owning handle) + loop:: factory
// ----------------------------------------------------------------------------

#include "ve_test.h"
#include "ve/core/loop.h"
#include "ve/core/object.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace ve;

// Anonymous single-thread asio loop for tests, via the public factory.
static Loop makeLoop() { return loop::asio()(); }

// --- basic lifecycle ---

VE_TEST(loop_create_destroy) {
    Loop loop = makeLoop();
    VE_ASSERT(!!loop);
    VE_ASSERT(!loop.isRunning());
}

VE_TEST(loop_start_stop) {
    Loop loop = makeLoop();
    VE_ASSERT(loop.start());
    VE_ASSERT(loop.isRunning());
    VE_ASSERT(loop.stop());
    VE_ASSERT(!loop.isRunning());
}

VE_TEST(loop_double_start) {
    Loop loop = makeLoop();
    VE_ASSERT(loop.start());
    VE_ASSERT(!loop.start());  // already running
    loop.stop();
}

VE_TEST(loop_double_stop) {
    Loop loop = makeLoop();
    loop.start();
    VE_ASSERT(loop.stop());
    VE_ASSERT(!loop.stop());   // already stopped
}

// --- post ---

VE_TEST(loop_post_single) {
    Loop loop = makeLoop();
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
    Loop loop = makeLoop();
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
    // A single-threaded loop must still accept posts from many threads safely.
    Loop loop = makeLoop();
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

// --- handle copy / non-owning view ---

VE_TEST(loop_handle_copy_posts) {
    Loop loop = makeLoop();
    loop.start();

    Loop ref = loop;          // copy is another non-owning view of the same backend
    VE_ASSERT(!!ref);

    std::atomic<int> val{0};
    ref.post([&] { val.store(99); });

    for (int i = 0; i < 100 && val.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    VE_ASSERT_EQ(val.load(), 99);
    loop.stop();
}

VE_TEST(loop_empty_handle) {
    Loop ref;
    VE_ASSERT(!ref);
    ref.post([] {});  // should not crash — empty handle drops the task
}

VE_TEST(loop_handle_is_non_owning_view) {
    // Handles are bare views into the permanent store: a copy keeps working
    // after the original handle is gone, because neither handle owns the backend
    // (the store does, for the life of the process).
    Loop ref;
    {
        Loop loop = makeLoop();
        loop.start();
        ref = loop;
        VE_ASSERT(!!ref);
    }  // original handle goes out of scope; backend lives on in the store

    std::atomic<int> val{0};
    ref.post([&] { val.store(7); });

    for (int i = 0; i < 100 && val.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    VE_ASSERT_EQ(val.load(), 7);  // loop still running via the surviving view
    ref.stop();
}

// --- global loop ---

VE_TEST(loop_global_main) {
    const Loop& m = loop::main();
    VE_ASSERT(m.isRunning());

    std::atomic<int> val{0};
    m.post([&] { val.store(1); });

    for (int i = 0; i < 100 && val.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    VE_ASSERT_EQ(val.load(), 1);
}

VE_TEST(loop_global_pool) {
    const Loop& p = loop::pool();
    VE_ASSERT(p.isRunning());
}

// --- named-loop factory ---

VE_TEST(loop_factory_reg_instance) {
    // reg records a lazy factory; instance() materializes once and is stable.
    VE_ASSERT(loop::reg("test.named", loop::asio()));   // installed
    Loop a = loop::instance("test.named");
    Loop b = loop::instance("test.named");
    VE_ASSERT(!!a);
    VE_ASSERT(a._opsPtr() == b._opsPtr());              // same backend, not rebuilt

    a.start();
    std::atomic<int> val{0};
    b.post([&] { val.store(5); });                      // post via the second handle
    for (int i = 0; i < 100 && val.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    VE_ASSERT_EQ(val.load(), 5);
    a.stop();
}

VE_TEST(loop_factory_first_reg_wins) {
    // A pre-installed factory pre-empts a later default — the override mechanism
    // veQt uses to make loop::instance("main") land on the Qt main thread.
    std::atomic<int> built{0};
    auto counting = [&]() -> Loop { built.fetch_add(1); return loop::asio()(); };

    VE_ASSERT(loop::reg("test.override", counting));    // first wins
    VE_ASSERT(!loop::reg("test.override", loop::asio())); // ignored
    loop::instance("test.override");                    // materialize
    loop::instance("test.override");
    VE_ASSERT_EQ(built.load(), 1);                      // built once, via first factory
}

VE_TEST(loop_factory_unregistered_is_empty) {
    VE_ASSERT(!loop::instance("test.never.registered"));
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
    Loop loop = makeLoop();
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
    auto token = Token::create();
    Loop loop = makeLoop();
    loop.start();

    std::atomic<int> val{0};
    loop.post(Task{[&] { val.store(1); }, {token}});

    for (int i = 0; i < 100 && val.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    VE_ASSERT_EQ(val.load(), 1);
    loop.stop();
}

VE_TEST(loop_alive_token_dead) {
    auto token = Token::create();
    Loop loop = makeLoop();
    loop.start();

    std::atomic<int> val{0};
    token.kill();  // "dead" before task executes
    loop.post(Task{[&] { val.store(99); }, {token}});

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    VE_ASSERT_EQ(val.load(), 0);  // task was discarded
    loop.stop();
}

VE_TEST(loop_post_token_alive) {
    auto token = Token::create();
    std::atomic<int> val{0};

    loop::post(Task{[&] { val.store(42); }, {token}});

    for (int i = 0; i < 100 && val.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    VE_ASSERT_EQ(val.load(), 42);
}

VE_TEST(loop_post_token_dead) {
    auto token = Token::create();
    Loop loop = makeLoop();
    std::atomic<int> val{0};

    loop.post(Task{[&] { val.store(99); }, {token}});
    token.kill();  // "dead" before task executes

    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    VE_ASSERT_EQ(val.load(), 0);  // task discarded
    loop.stop();
}

VE_TEST(loop_post_token_identity) {
    // The token a task is posted with becomes loop::token() while it runs;
    // the token's owner address is recoverable via loop::token().owner()/as<T>().
    int dummy;
    Token token = Token::create(&dummy);
    std::atomic<void*> captured_owner{nullptr};
    std::atomic<bool>  ran{false};

    loop::post(Task{[&] {
        captured_owner.store(loop::token().owner());
        ran.store(true);
    }, {token}});

    for (int i = 0; i < 100 && !ran.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    VE_ASSERT_EQ(captured_owner.load(), static_cast<void*>(&dummy));
}

VE_TEST(loop_token_null_outside) {
    VE_ASSERT(!loop::token());                       // empty token outside any loop task
    VE_ASSERT(loop::token().owner() == nullptr);
}

// --- signal dispatch with loop: observer destroyed → no crash ---

VE_TEST(loop_signal_observer_destroyed) {
    Loop loop = makeLoop();
    loop.start();

    Object sender("sender");
    auto* observer = new Object("observer");

    std::atomic<int> val{0};
    sender.connect<1>(observer, [&]() { val.store(1); }, loop);

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

VE_TEST(loop_signal_observer_destroyed_inflight) {
    // P2: observer destroyed AFTER the task is enqueued but BEFORE the loop
    // drains it. The Task guards on the receiver token, so it must be dropped —
    // not run on a freed observer.
    Loop loop = makeLoop();

    Object sender("sender");
    auto* observer = new Object("observer");

    std::atomic<int> val{0};
    sender.connect<1>(observer, [&]() { val.store(1); }, loop);

    // queue task while loop is stopped, then destroy observer before draining
    sender.trigger<1>();
    delete observer;

    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    VE_ASSERT_EQ(val.load(), 0);  // task discarded: receiver token is dead
    loop.stop();
}

VE_TEST(loop_signal_sender_destroyed) {
    Loop loop = makeLoop();

    auto* sender = new Object("sender");
    Object observer("observer");

    std::atomic<int> val{0};
    sender->connect<1>(&observer, [&]() { val.store(1); }, loop);

    // queue task while loop is stopped, then destroy sender before starting
    sender->trigger<1>();
    delete sender;

    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    VE_ASSERT_EQ(val.load(), 0);  // task discarded: sender token is dead
    loop.stop();
}
