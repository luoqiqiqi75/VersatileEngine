// ----------------------------------------------------------------------------
// test_loop.cpp - ve::Loop virtual base + pointer dispatch
// ----------------------------------------------------------------------------

#include "ve_test.h"
#include "ve/core/loop.h"
#include "ve/core/object.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

using namespace ve;

namespace {

static bool waitUntil(const std::function<bool()>& fn, int timeout_ms = 1000)
{
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::milliseconds(timeout_ms);
    while (clock::now() < deadline) {
        if (fn()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return fn();
}

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

    VE_ASSERT(waitUntil([&] { return val.load() == 42; }));
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

    VE_ASSERT(waitUntil([&] { return count.load() == 1000; }, 1500));
    loop.stop();
}

VE_TEST(loop_global_main_pointer) {
    Loop* m = loop::main();
    VE_ASSERT(m != nullptr);
    VE_ASSERT(m->isRunning());

    std::atomic<int> val{0};
    m->post([&] { val.store(1); });

    VE_ASSERT(waitUntil([&] { return val.load() == 1; }));
}

VE_TEST(loop_global_pool_pointer) {
    Loop* p = loop::pool();
    VE_ASSERT(p != nullptr);
    VE_ASSERT(p->isRunning());
}

VE_TEST(loop_convenience_post) {
    std::atomic<int> val{0};
    loop::main()->post([&] { val.store(77); });

    VE_ASSERT(waitUntil([&] { return val.load() == 77; }));
}

VE_TEST(loop_signal_observer_destroyed) {
    TestLoop loop;
    loop.start();

    Object sender("sender");
    auto* observer = new Object("observer");

    std::atomic<int> val{0};
    sender.connect<1>(observer, [&]() { val.store(1); }, &loop);

    sender.trigger<1>();
    VE_ASSERT(waitUntil([&] { return val.load() == 1; }));

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
    VE_ASSERT(waitUntil([&] { return seen.load() == &sender; }));
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
