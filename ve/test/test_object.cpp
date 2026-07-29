// ----------------------------------------------------------------------------
// test_object.cpp — ve::Object lifecycle + signal/slot + thread safety
// ----------------------------------------------------------------------------

#include "ve_test.h"
#include "ve/core/object.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace ve;

VE_TEST(object_default_name) {
    Object obj;
    VE_ASSERT_EQ(obj.name(), "");
}

VE_TEST(object_custom_name) {
    Object obj("sensor");
    VE_ASSERT_EQ(obj.name(), "sensor");
}

// --- signal/slot ---

VE_TEST(object_connect_trigger) {
    Object src("src");
    Object obs("obs");
    int count = 0;
    src.connect<1>(&obs, [&](const Var&) { count++; });
    src.trigger<1>();
    VE_ASSERT_EQ(count, 1);
    src.trigger<1>();
    VE_ASSERT_EQ(count, 2);
}

VE_TEST(object_trigger_no_connection) {
    Object src;
    src.trigger<999>();  // should not crash
}

VE_TEST(object_multiple_observers) {
    Object src;
    Object obs1("o1"), obs2("o2");
    int c1 = 0, c2 = 0;
    src.connect<1>(&obs1, [&](const Var&) { c1++; });
    src.connect<1>(&obs2, [&](const Var&) { c2++; });
    src.trigger<1>();
    VE_ASSERT_EQ(c1, 1);
    VE_ASSERT_EQ(c2, 1);
}

VE_TEST(object_disconnect_signal_observer) {
    Object src;
    Object obs;
    int count = 0;
    src.connect<1>(&obs, [&](const Var&) { count++; });
    src.disconnect<1>(&obs);
    src.trigger<1>();
    VE_ASSERT_EQ(count, 0);
}

VE_TEST(object_disconnect_all_from_observer) {
    Object src;
    Object obs;
    int c1 = 0, c2 = 0;
    src.connect<1>(&obs, [&](const Var&) { c1++; });
    src.connect<2>(&obs, [&](const Var&) { c2++; });
    src.disconnect(&obs);
    src.trigger<1>();
    src.trigger<2>();
    VE_ASSERT_EQ(c1, 0);
    VE_ASSERT_EQ(c2, 0);
}

VE_TEST(object_hasConnection) {
    Object src;
    Object obs;
    VE_ASSERT(!src.hasConnection<1>(&obs));
    src.connect<1>(&obs, [](const Var&) {});
    VE_ASSERT(src.hasConnection<1>(&obs));
    src.disconnect<1>(&obs);
    VE_ASSERT(!src.hasConnection<1>(&obs));
}

VE_TEST(object_hasConnection_any) {
    Object src;
    Object obs;
    VE_ASSERT(!src.hasConnection<1>());
    src.connect<1>(&obs, [](const Var&) {});
    VE_ASSERT(src.hasConnection<1>());
    src.disconnect<1>(&obs);
    VE_ASSERT(!src.hasConnection<1>());
}

// --- name immutability ---

VE_TEST(object_name_immutable) {
    Object obj("fixed");
    VE_ASSERT_EQ(obj.name(), "fixed");
    // name has no setter — remains fixed for lifetime
    VE_ASSERT_EQ(obj.name(), "fixed");
}

VE_TEST(object_empty_name_stays_empty) {
    Object obj;
    VE_ASSERT_EQ(obj.name(), "");
}

// --- thread safety ---

VE_TEST(object_thread_concurrent_trigger) {
    Object src("src");
    Object obs("obs");
    std::atomic<int> count{0};

    src.connect<1>(&obs, [&](const Var&) { count.fetch_add(1, std::memory_order_relaxed); });

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
        threads.emplace_back([&] { for (int i = 0; i < 500; ++i) src.trigger<1>(); });
    for (auto& th : threads) th.join();

    VE_ASSERT_EQ(count.load(), 2000);
}

VE_TEST(object_thread_connect_disconnect) {
    Object src("src");
    Object obs1("o1"), obs2("o2"), obs3("o3"), obs4("o4");
    Object* obs[] = { &obs1, &obs2, &obs3, &obs4 };
    std::atomic<bool> go{false};

    auto worker = [&](int idx) {
        while (!go.load()) {}
        for (int i = 0; i < 200; ++i) {
            src.connect<1>(obs[idx], [](const Var&) {});
            src.trigger<1>();
            src.disconnect<1>(obs[idx]);
        }
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) threads.emplace_back(worker, t);
    go.store(true);
    for (auto& th : threads) th.join();

    // no crash = thread safety OK
    VE_ASSERT(true);
}

VE_TEST(object_thread_name_read) {
    Object obj("immutable_name");
    std::atomic<bool> ok{true};

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < 1000; ++i)
                if (obj.name() != "immutable_name") ok.store(false);
        });
    }
    for (auto& th : threads) th.join();

    VE_ASSERT(ok.load());
}

VE_TEST(object_thread_mutex_external) {
    Object obj("mtx_test");
    std::atomic<int> sum{0};

    auto worker = [&] {
        for (int i = 0; i < 500; ++i) {
            std::lock_guard<std::recursive_mutex> lk(obj.mutex());
            sum.fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) threads.emplace_back(worker);
    for (auto& th : threads) th.join();

    VE_ASSERT_EQ(sum.load(), 2000);
}

// --- SignalT compatibility: users should be able to use plain int as signals ---

VE_TEST(object_signal_int_literal) {
    Object src("src"); Object obs("obs");
    int count = 0;
    src.connect<42>(&obs, [&]() { count++; });
    src.trigger<42>();
    VE_ASSERT_EQ(count, 1);
}

VE_TEST(object_signal_enum_int) {
    enum : int { MY_SIG = 100 };
    Object src("src"); Object obs("obs");
    int count = 0;
    src.connect<MY_SIG>(&obs, [&]() { count++; });
    src.trigger<MY_SIG>();
    VE_ASSERT_EQ(count, 1);
}

VE_TEST(object_signal_constexpr_int) {
    constexpr int SIG = 200;
    Object src("src"); Object obs("obs");
    int count = 0;
    src.connect<SIG>(&obs, [&]() { count++; });
    src.trigger<SIG>();
    VE_ASSERT_EQ(count, 1);
}

VE_TEST(object_signal_runtime_int_connect) {
    Object src("src"); Object obs("obs");
    int count = 0;
    int sig = 300;
    src.connect(sig, &obs, [&]() { count++; });
    src.trigger<300>();
    VE_ASSERT_EQ(count, 1);
}

// --- timers: owned by the Object, scheduled by a Loop ---

VE_TEST(object_timer_repeat) {
    AsioLoop loop("timer.repeat");
    loop.start();

    Object src("src"), obs("obs");
    std::atomic<int> ticks{0};
    std::atomic<int64_t> last{0};

    auto id = src.startTimer(5, true, &loop);
    VE_ASSERT(id >= Object::TIMER_BASE);
    VE_ASSERT(src.hasTimer(id));
    src.connect(id, &obs, [&](int64_t n) { last.store(n); ticks.fetch_add(1); });

    VE_ASSERT(VE_WAIT([&] { return ticks.load() >= 3; }));
    VE_ASSERT_EQ(last.load(), (int64_t)ticks.load());   // 1-based, monotonic

    VE_ASSERT(src.killTimer(id));
    VE_ASSERT(!src.hasTimer(id));
    VE_ASSERT(!src.killTimer(id));

    const int settled = ticks.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    VE_ASSERT_EQ(ticks.load(), settled);               // killTimer drops connections too

    loop.stop();
}

VE_TEST(object_timer_single_shot) {
    AsioLoop loop("timer.once");
    loop.start();

    Object src("src"), obs("obs");
    std::atomic<int> ticks{0};

    auto id = src.startTimer(5, false, &loop);
    src.connect(id, &obs, [&]() { ticks.fetch_add(1); });

    VE_ASSERT(VE_WAIT([&] { return ticks.load() == 1; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    VE_ASSERT_EQ(ticks.load(), 1);
    VE_ASSERT(!src.hasTimer(id));                      // self-removed after firing

    loop.stop();
}

VE_TEST(object_timer_dies_with_owner) {
    AsioLoop loop("timer.owner");
    loop.start();

    std::atomic<int> ticks{0};
    Object obs("obs");
    {
        Object src("src");
        auto id = src.startTimer(2, true, &loop);
        src.connect(id, &obs, [&]() { ticks.fetch_add(1); });
        VE_ASSERT(VE_WAIT([&] { return ticks.load() >= 2; }));
    }   // ~Object kills the timer and drains any in-flight tick

    const int settled = ticks.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    VE_ASSERT_EQ(ticks.load(), settled);

    loop.stop();
}

VE_TEST(object_timer_kill_from_slot) {
    AsioLoop loop("timer.selfkill");
    loop.start();

    Object src("src"), obs("obs");
    std::atomic<int> ticks{0};

    auto id = src.startTimer(2, true, &loop);
    src.connect(id, &obs, [&](int64_t) { ticks.fetch_add(1); src.killTimer(id); });

    VE_ASSERT(VE_WAIT([&] { return ticks.load() == 1; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    VE_ASSERT_EQ(ticks.load(), 1);

    loop.stop();
}

VE_TEST(object_timer_no_scheduler) {
    Loop plain("plain");           // no scheduler behind it
    Object src("src");
    VE_ASSERT_EQ(src.startTimer(5, true, &plain), (Object::SignalT)0);
}

VE_TEST(object_timer_defaults_to_current_loop) {
    AsioLoop loop("timer.current");
    loop.start();

    Object src("src"), obs("obs");
    std::atomic<int> ticks{0};
    std::atomic<Object::SignalT> id{0};

    // startTimer() with no loop picks the loop running the call.
    loop.post([&] {
        auto tid = src.startTimer(3, true);
        src.connect(tid, &obs, [&]() { ticks.fetch_add(1); });
        id.store(tid);
    });

    VE_ASSERT(VE_WAIT([&] { return ticks.load() >= 2; }));
    src.killTimer(id.load());
    loop.stop();
}

// --- timers: thread safety ---
//
// These exist to trip the races the implementation is built around, not to
// assert timing. Run them under TSan/ASan when touching the timer paths.

VE_TEST(object_timer_concurrent_start_kill) {
    AsioPoolLoop pool("timer.race.pool", 4);
    pool.start();

    Object src("src"), obs("obs");
    std::atomic<int> ticks{0};

    std::vector<std::thread> threads;
    std::mutex ids_mtx;
    std::vector<Object::SignalT> ids;

    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < 50; ++i) {
                auto id = src.startTimer(1, true, &pool);
                src.connect(id, &obs, [&]() { ticks.fetch_add(1); });
                std::lock_guard<std::mutex> lk(ids_mtx);
                ids.push_back(id);
            }
        });
    }
    for (auto& th : threads) th.join();
    VE_ASSERT_EQ((int)ids.size(), 200);

    std::vector<std::thread> killers;
    for (int t = 0; t < 4; ++t) {
        killers.emplace_back([&, t] {
            for (size_t i = t; i < ids.size(); i += 4) src.killTimer(ids[i]);
        });
    }
    for (auto& th : killers) th.join();

    for (auto id : ids) VE_ASSERT(!src.hasTimer(id));

    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    const int settled = ticks.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    VE_ASSERT_EQ(ticks.load(), settled);   // nothing survived

    pool.stop();
}

VE_TEST(object_timer_ids_are_unique_across_threads) {
    AsioLoop lp("timer.ids");
    lp.start();

    Object src("src");
    std::mutex mtx;
    std::vector<Object::SignalT> ids;

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < 50; ++i) {
                auto id = src.startTimer(60000, true, &lp);   // never fires
                std::lock_guard<std::mutex> lk(mtx);
                ids.push_back(id);
            }
        });
    }
    for (auto& th : threads) th.join();

    std::sort(ids.begin(), ids.end());
    VE_ASSERT(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
    VE_ASSERT(ids.front() >= Object::TIMER_BASE);

    src.killTimers();
    for (auto id : ids) VE_ASSERT(!src.hasTimer(id));
    lp.stop();
}

VE_TEST(object_timer_destroy_owner_while_ticking) {
    AsioPoolLoop pool("timer.destroy", 4);
    pool.start();

    Object obs("obs");
    std::atomic<int> ticks{0};

    // Each owner ticks fast with a slow slot, so destruction below is very
    // likely to land while a tick is mid-flight on a pool thread.
    for (int round = 0; round < 20; ++round) {
        Object src("src");
        auto id = src.startTimer(1, true, &pool);
        src.connect(id, &obs, [&]() {
            ticks.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }   // ~Object drains the in-flight tick each round

    VE_ASSERT(ticks.load() > 0);
    pool.stop();
}

VE_TEST(object_timer_kill_from_another_thread_while_ticking) {
    AsioLoop lp("timer.kill.race");
    lp.start();

    Object src("src"), obs("obs");
    std::atomic<int> ticks{0};

    auto id = src.startTimer(1, true, &lp);
    src.connect(id, &obs, [&]() {
        ticks.fetch_add(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    });

    VE_ASSERT(VE_WAIT([&] { return ticks.load() >= 2; }));

    std::atomic<bool> killed{false};
    std::thread killer([&] { killed.store(src.killTimer(id)); });
    killer.join();
    VE_ASSERT(killed.load());

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const int settled = ticks.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    VE_ASSERT_EQ(ticks.load(), settled);

    lp.stop();
}

VE_TEST(object_timer_many_owners_on_shared_pool) {
    AsioPoolLoop pool("timer.many", 4);
    pool.start();

    std::atomic<int> ticks{0};
    std::vector<std::unique_ptr<Object>> owners;
    Object obs("obs");

    for (int i = 0; i < 32; ++i) {
        auto o = std::make_unique<Object>("owner");
        auto id = o->startTimer(2, true, &pool);
        o->connect(id, &obs, [&]() { ticks.fetch_add(1); });
        owners.push_back(std::move(o));
    }

    VE_ASSERT(VE_WAIT([&] { return ticks.load() >= 64; }));

    // Tear down from several threads at once.
    std::vector<std::thread> threads;
    std::mutex mtx;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&] {
            for (;;) {
                std::unique_ptr<Object> victim;
                {
                    std::lock_guard<std::mutex> lk(mtx);
                    if (owners.empty()) return;
                    victim = std::move(owners.back());
                    owners.pop_back();
                }
                victim.reset();
            }
        });
    }
    for (auto& th : threads) th.join();

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const int settled = ticks.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    VE_ASSERT_EQ(ticks.load(), settled);

    pool.stop();
}

VE_TEST(object_timer_silent_suppresses_ticks) {
    AsioLoop lp("timer.silent");
    lp.start();

    Object src("src"), obs("obs");
    std::atomic<int> ticks{0};

    src.silent(true);
    auto id = src.startTimer(2, true, &lp);
    src.connect(id, &obs, [&]() { ticks.fetch_add(1); });

    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    VE_ASSERT_EQ(ticks.load(), 0);         // SILENT gates trigger()
    VE_ASSERT(src.hasTimer(id));           // ...but the schedule is still live

    src.silent(false);
    VE_ASSERT(VE_WAIT([&] { return ticks.load() >= 2; }));

    src.killTimer(id);
    lp.stop();
}

VE_TEST(object_timer_survives_throwing_slot) {
    AsioLoop lp("timer.throw");
    lp.start();

    Object src("src"), obs("obs");
    std::atomic<int> ticks{0};

    auto id = src.startTimer(2, true, &lp);
    src.connect(id, &obs, [&]() {
        ticks.fetch_add(1);
        throw std::runtime_error("boom");
    });

    // The gate must be released even when the slot throws, or ~Object hangs.
    VE_ASSERT(VE_WAIT([&] { return ticks.load() >= 1; }));
    src.killTimer(id);
    lp.stop();
}
