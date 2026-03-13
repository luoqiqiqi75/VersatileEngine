// ----------------------------------------------------------------------------
// test_object.cpp — ve::Object lifecycle + signal/slot + thread safety
// ----------------------------------------------------------------------------

#include "ve_test.h"
#include "ve/core/object.h"
#include <thread>
#include <atomic>
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
    src.connect(1, &obs, [&] { count++; });
    src.trigger(1);
    VE_ASSERT_EQ(count, 1);
    src.trigger(1);
    VE_ASSERT_EQ(count, 2);
}

VE_TEST(object_trigger_no_connection) {
    Object src;
    src.trigger(999);  // should not crash
}

VE_TEST(object_multiple_observers) {
    Object src;
    Object obs1("o1"), obs2("o2");
    int c1 = 0, c2 = 0;
    src.connect(1, &obs1, [&] { c1++; });
    src.connect(1, &obs2, [&] { c2++; });
    src.trigger(1);
    VE_ASSERT_EQ(c1, 1);
    VE_ASSERT_EQ(c2, 1);
}

VE_TEST(object_disconnect_signal_observer) {
    Object src;
    Object obs;
    int count = 0;
    src.connect(1, &obs, [&] { count++; });
    src.disconnect(1, &obs);
    src.trigger(1);
    VE_ASSERT_EQ(count, 0);
}

VE_TEST(object_disconnect_all_from_observer) {
    Object src;
    Object obs;
    int c1 = 0, c2 = 0;
    src.connect(1, &obs, [&] { c1++; });
    src.connect(2, &obs, [&] { c2++; });
    src.disconnect(&obs);
    src.trigger(1);
    src.trigger(2);
    VE_ASSERT_EQ(c1, 0);
    VE_ASSERT_EQ(c2, 0);
}

VE_TEST(object_hasConnection) {
    Object src;
    Object obs;
    VE_ASSERT(!src.hasConnection(1, &obs));
    src.connect(1, &obs, [] {});
    VE_ASSERT(src.hasConnection(1, &obs));
    src.disconnect(1, &obs);
    VE_ASSERT(!src.hasConnection(1, &obs));
}

VE_TEST(object_deleted_signal) {
    int count = 0;
    Object obs;
    {
        Object src("temp");
        src.connect(Object::OBJECT_DELETED, &obs, [&] { count++; });
    }
    // src destroyed — OBJECT_DELETED should have fired
    VE_ASSERT_EQ(count, 1);
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

    src.connect(1, &obs, [&] { count.fetch_add(1, std::memory_order_relaxed); });

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
        threads.emplace_back([&] { for (int i = 0; i < 500; ++i) src.trigger(1); });
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
            src.connect(1, obs[idx], [] {});
            src.trigger(1);
            src.disconnect(1, obs[idx]);
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
