// ----------------------------------------------------------------------------
// test_loop.cpp - ve::Loop virtual base + pointer dispatch
// ----------------------------------------------------------------------------

#include "ve_test.h"
#include "ve/core/loop.h"
#include "ve/core/object.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace ve;

namespace {

class TestLoop : public Loop
{
    std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<Task> tasks_;
    std::thread worker_;
    bool running_ = false;
    bool quit_ = false;

public:
    void post(Task task) override
    {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            tasks_.push_back(std::move(task));
        }
        cv_.notify_one();
    }

    explicit TestLoop(const std::string& name = "test")
        : Loop(name)
    {}

    ~TestLoop() override { stop(); }

    bool start() override
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (running_) return false;
        quit_ = false;
        running_ = true;
        worker_ = std::thread([this] {
            for (;;) {
                Task task;
                {
                    std::unique_lock<std::mutex> lk(mtx_);
                    cv_.wait(lk, [&] { return quit_ || !tasks_.empty(); });
                    if (quit_ && tasks_.empty()) break;
                    task = std::move(tasks_.front());
                    tasks_.pop_front();
                }
                if (task) task();
            }
        });
        return true;
    }

    bool stop() override
    {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (!running_) return false;
            quit_ = true;
            running_ = false;
        }
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
        return true;
    }

    bool isRunning() const override { return running_; }
};

} // namespace

VE_TEST(loop_default_inline_post) {
    Loop loop("inline");
    std::atomic<int> val{0};
    loop.post([&] { val.store(1); });
    VE_ASSERT_EQ(val.load(), 1);
    VE_ASSERT(!loop.isRunning());
}

VE_TEST(loop_custom_start_stop) {
    TestLoop loop;
    VE_ASSERT(!loop.isRunning());
    VE_ASSERT(loop.start());
    VE_ASSERT(loop.isRunning());
    VE_ASSERT(!loop.start());
    VE_ASSERT(loop.stop());
    VE_ASSERT(!loop.isRunning());
    VE_ASSERT(!loop.stop());
}

VE_TEST(loop_custom_post_single) {
    TestLoop loop;
    loop.start();

    std::atomic<int> val{0};
    loop.post([&] { val.store(42); });

    VE_ASSERT(VE_WAIT([&] { return val.load() == 42; }));
    loop.stop();
}

VE_TEST(loop_custom_post_from_threads) {
    TestLoop loop;
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

    VE_ASSERT(VE_WAIT([&] { return count.load() == 1000; }, 1500));
    loop.stop();
}

VE_TEST(loop_asio_direct_construct) {
    AsioLoop loop("direct.asio");
    VE_ASSERT(loop.start());

    std::atomic<int> val{0};
    loop.post([&] { val.store(12); });

    VE_ASSERT(VE_WAIT([&] { return val.load() == 12; }));
    loop.stop();
}

VE_TEST(loop_global_main_pointer) {
    Loop* m = loop::main();
    VE_ASSERT(m != nullptr);
    VE_ASSERT(m->isRunning());

    std::atomic<int> val{0};
    m->post([&] { val.store(1); });

    VE_ASSERT(VE_WAIT([&] { return val.load() == 1; }));
}

VE_TEST(loop_global_pool_pointer) {
    Loop* p = loop::pool();
    VE_ASSERT(p != nullptr);
    VE_ASSERT(p->isRunning());
}

VE_TEST(loop_set_main_borrowed_pointer) {
    TestLoop custom("custom.main");
    custom.start();

    Loop* original = loop::main();
    loop::setMain(&custom);
    VE_ASSERT(loop::main() == &custom);

    std::atomic<int> val{0};
    loop::main()->post([&] { val.store(77); });

    VE_ASSERT(VE_WAIT([&] { return val.load() == 77; }));

    loop::setMain(nullptr);
    VE_ASSERT(loop::main() == original);
    custom.stop();
}

VE_TEST(loop_signal_observer_destroyed) {
    TestLoop loop;
    loop.start();

    Object sender("sender");
    auto* observer = new Object("observer");

    std::atomic<int> val{0};
    sender.connect<1>(observer, [&]() { val.store(1); }, &loop);

    sender.trigger<1>();
    VE_ASSERT(VE_WAIT([&] { return val.load() == 1; }));

    delete observer;
    val.store(0);
    sender.trigger<1>();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    VE_ASSERT_EQ(val.load(), 0);

    loop.stop();
}

VE_TEST(object_sender_direct) {
    Object sender("sender");
    Object observer("observer");

    Object* seen = nullptr;
    sender.connect<1>(&observer, [&]() { seen = Object::sender(); });

    sender.trigger<1>();
    VE_ASSERT_EQ(seen, &sender);
    VE_ASSERT(Object::sender() == nullptr);
}

VE_TEST(object_sender_queued) {
    TestLoop loop;
    loop.start();

    Object sender("sender");
    Object observer("observer");

    std::atomic<Object*> seen{nullptr};
    sender.connect<1>(&observer, [&]() { seen.store(Object::sender()); }, &loop);

    sender.trigger<1>();
    VE_ASSERT(VE_WAIT([&] { return seen.load() == &sender; }));
    VE_ASSERT(Object::sender() == nullptr);

    loop.stop();
}

VE_TEST(loop_signal_observer_destroyed_inflight) {
    TestLoop loop;

    Object sender("sender");
    auto* observer = new Object("observer");

    std::atomic<int> val{0};
    sender.connect<1>(observer, [&]() { val.store(1); }, &loop);

    sender.trigger<1>();
    delete observer;

    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    VE_ASSERT_EQ(val.load(), 0);
    loop.stop();
}

VE_TEST(loop_signal_sender_destroyed) {
    TestLoop loop;

    auto* sender = new Object("sender");
    Object observer("observer");

    std::atomic<int> val{0};
    sender->connect<1>(&observer, [&]() { val.store(1); }, &loop);

    sender->trigger<1>();
    delete sender;

    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    VE_ASSERT_EQ(val.load(), 0);
    loop.stop();
}

// --- scheduling primitive: raw handles, no owner ---

VE_TEST(loop_plain_has_no_scheduler) {
    Loop plain("plain");
    VE_ASSERT_EQ(plain.addTimer(5, true, [] {}), (Loop::TimerHandle)0);
    VE_ASSERT(!plain.removeTimer(1));
}

VE_TEST(loop_add_timer_rejects_empty_tick) {
    AsioLoop loop("timer.empty");
    loop.start();
    VE_ASSERT_EQ(loop.addTimer(5, true, nullptr), (Loop::TimerHandle)0);
    loop.stop();
}

VE_TEST(loop_add_timer_repeat_and_remove) {
    AsioLoop loop("timer.raw");
    loop.start();

    std::atomic<int> ticks{0};
    auto h = loop.addTimer(5, true, [&] { ticks.fetch_add(1); });
    VE_ASSERT(h != 0);
    VE_ASSERT(VE_WAIT([&] { return ticks.load() >= 3; }));

    VE_ASSERT(loop.removeTimer(h));
    VE_ASSERT(!loop.removeTimer(h));

    const int settled = ticks.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    VE_ASSERT_EQ(ticks.load(), settled);

    loop.stop();
}

VE_TEST(loop_add_timer_single_shot) {
    AsioLoop loop("timer.raw.once");
    loop.start();

    std::atomic<int> ticks{0};
    loop.addTimer(5, false, [&] { ticks.fetch_add(1); });

    VE_ASSERT(VE_WAIT([&] { return ticks.load() == 1; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    VE_ASSERT_EQ(ticks.load(), 1);

    loop.stop();
}

VE_TEST(loop_timers_die_with_stop) {
    AsioLoop loop("timer.stop");
    loop.start();

    std::atomic<int> ticks{0};
    loop.addTimer(2, true, [&] { ticks.fetch_add(1); });
    VE_ASSERT(VE_WAIT([&] { return ticks.load() >= 2; }));

    loop.stop();
    const int settled = ticks.load();
    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    VE_ASSERT_EQ(ticks.load(), settled);   // stop() clears the schedule
    loop.stop();
}

VE_TEST(loop_timer_ticks_run_on_loop_thread) {
    AsioLoop lp("timer.thread");
    lp.start();

    std::atomic<bool> seen{false};
    std::atomic<bool> on_loop{false};
    lp.addTimer(2, false, [&] {
        on_loop.store(loop::current() == &lp);
        seen.store(true);
    });

    VE_ASSERT(VE_WAIT([&] { return seen.load(); }));
    VE_ASSERT(on_loop.load());
    lp.stop();
}

VE_TEST(loop_pool_timer) {
    AsioPoolLoop pool("timer.pool", 2);
    pool.start();

    std::atomic<int> ticks{0};
    auto h = pool.addTimer(5, true, [&] { ticks.fetch_add(1); });
    VE_ASSERT(h != 0);
    VE_ASSERT(VE_WAIT([&] { return ticks.load() >= 3; }));

    pool.removeTimer(h);
    pool.stop();
}

// --- Loop timers: thread safety ---

VE_TEST(loop_timer_concurrent_add_remove) {
    AsioPoolLoop pool("timer.race", 4);
    pool.start();

    std::atomic<int> ticks{0};
    std::mutex mtx;
    std::vector<Loop::TimerHandle> handles;

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < 50; ++i) {
                auto h = pool.addTimer(1, true, [&] { ticks.fetch_add(1); });
                std::lock_guard<std::mutex> lk(mtx);
                handles.push_back(h);
            }
        });
    }
    for (auto& th : threads) th.join();
    VE_ASSERT_EQ((int)handles.size(), 200);

    std::vector<std::thread> killers;
    for (int t = 0; t < 4; ++t) {
        killers.emplace_back([&, t] {
            for (size_t i = t; i < handles.size(); i += 4) pool.removeTimer(handles[i]);
        });
    }
    for (auto& th : killers) th.join();

    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    const int settled = ticks.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    VE_ASSERT_EQ(ticks.load(), settled);

    pool.stop();
}

VE_TEST(loop_timer_remove_beats_first_tick) {
    AsioLoop lp("timer.early.remove");
    lp.start();

    std::atomic<int> ticks{0};
    // Removed while the arming post is still in flight — must never fire.
    auto h = lp.addTimer(1, true, [&] { ticks.fetch_add(1); });
    VE_ASSERT(lp.removeTimer(h));

    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    VE_ASSERT_EQ(ticks.load(), 0);
    lp.stop();
}

VE_TEST(loop_timer_remove_from_own_tick) {
    AsioLoop lp("timer.selfremove");
    lp.start();

    std::atomic<int> ticks{0};
    std::atomic<Loop::TimerHandle> h{0};
    std::atomic<bool> removed_self{false};

    h.store(lp.addTimer(2, true, [&] {
        ticks.fetch_add(1);
        removed_self.store(lp.removeTimer(h.load()));   // must not deadlock
    }));

    VE_ASSERT(VE_WAIT([&] { return ticks.load() == 1; }));
    VE_ASSERT(removed_self.load());
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    VE_ASSERT_EQ(ticks.load(), 1);
    lp.stop();
}

VE_TEST(loop_timer_slow_tick_does_not_burst) {
    AsioLoop lp("timer.slow");
    lp.start();

    std::atomic<int> ticks{0};
    // Interval 2ms, tick takes ~20ms: a catch-up scheduler would fire ~10x per
    // slot; fixed-rate-with-skip must stay roughly 1:1 with elapsed/20ms.
    auto h = lp.addTimer(2, true, [&] {
        ticks.fetch_add(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    });

    VE_ASSERT(VE_WAIT([&] { return ticks.load() >= 2; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    lp.removeTimer(h);

    VE_ASSERT(ticks.load() < 20);   // nowhere near a burst
    lp.stop();
}

VE_TEST(loop_timer_throwing_tick_keeps_loop_alive) {
    AsioLoop lp("timer.throw.raw");
    lp.start();

    std::atomic<int> ticks{0};
    auto h = lp.addTimer(2, true, [&] {
        ticks.fetch_add(1);
        throw std::runtime_error("boom");
    });

    // A tick that escapes into io.run() would terminate the worker thread.
    VE_ASSERT(VE_WAIT([&] { return ticks.load() >= 2; }));
    lp.removeTimer(h);

    std::atomic<int> posted{0};
    lp.post([&] { posted.store(1); });
    VE_ASSERT(VE_WAIT([&] { return posted.load() == 1; }));   // loop still serving
    lp.stop();
}

VE_TEST(loop_timer_handles_are_unique) {
    AsioLoop lp("timer.handles");
    lp.start();

    std::mutex mtx;
    std::vector<Loop::TimerHandle> handles;
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < 50; ++i) {
                auto h = lp.addTimer(60000, true, [] {});   // never fires
                std::lock_guard<std::mutex> lk(mtx);
                handles.push_back(h);
            }
        });
    }
    for (auto& th : threads) th.join();

    std::sort(handles.begin(), handles.end());
    VE_ASSERT(std::adjacent_find(handles.begin(), handles.end()) == handles.end());
    VE_ASSERT(handles.front() != 0);

    for (auto h : handles) lp.removeTimer(h);
    lp.stop();
}
