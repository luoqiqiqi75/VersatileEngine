// ----------------------------------------------------------------------------
// test_qt_loop.cpp — QtMainLoop / QtLoop timer scheduling
// ----------------------------------------------------------------------------

#include "qt_test.h"

#include "ve/qt/qt_loop.h"
#include "ve/core/object.h"

#include <QCoreApplication>
#include <QThread>

#include <atomic>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace ve;
using namespace ve::qt;

// --- Loop primitive ---

VE_TEST(qt_loop_add_timer_repeat) {
    QtMainLoop lp(QCoreApplication::instance());

    std::atomic<int> ticks{0};
    auto h = lp.addTimer(5, true, [&] { ticks.fetch_add(1); });
    VE_ASSERT(h != 0);

    VE_ASSERT(VE_QT_WAIT([&] { return ticks.load() >= 3; }));

    VE_ASSERT(lp.removeTimer(h));
    VE_ASSERT(!lp.removeTimer(h));

    const int settled = ticks.load();
    VE_QT_SETTLE();
    VE_ASSERT_EQ(ticks.load(), settled);
}

VE_TEST(qt_loop_add_timer_single_shot) {
    QtMainLoop lp(QCoreApplication::instance());

    std::atomic<int> ticks{0};
    lp.addTimer(5, false, [&] { ticks.fetch_add(1); });

    VE_ASSERT(VE_QT_WAIT([&] { return ticks.load() == 1; }));
    VE_QT_SETTLE();
    VE_ASSERT_EQ(ticks.load(), 1);
}

VE_TEST(qt_loop_rejects_empty_tick) {
    QtMainLoop lp(QCoreApplication::instance());
    VE_ASSERT_EQ(lp.addTimer(5, true, nullptr), (Loop::TimerHandle)0);
}

VE_TEST(qt_loop_without_app_has_no_scheduler) {
    QtLoop lp(nullptr, "qt.detached");   // no QEventLoop to attach to
    VE_ASSERT_EQ(lp.addTimer(5, true, [] {}), (Loop::TimerHandle)0);
    VE_ASSERT(!lp.removeTimer(1));
}

VE_TEST(qt_loop_tick_sets_current_loop) {
    QtMainLoop lp(QCoreApplication::instance());

    std::atomic<bool> seen{false};
    std::atomic<bool> on_loop{false};
    lp.addTimer(2, false, [&] {
        on_loop.store(loop::current() == &lp);
        seen.store(true);
    });

    VE_ASSERT(VE_QT_WAIT([&] { return seen.load(); }));
    VE_ASSERT(on_loop.load());
}

VE_TEST(qt_loop_tick_runs_on_gui_thread) {
    QtMainLoop lp(QCoreApplication::instance());

    std::atomic<bool> seen{false};
    std::atomic<bool> right_thread{false};
    QThread* gui = QCoreApplication::instance()->thread();

    lp.addTimer(2, false, [&] {
        right_thread.store(QThread::currentThread() == gui);
        seen.store(true);
    });

    VE_ASSERT(VE_QT_WAIT([&] { return seen.load(); }));
    VE_ASSERT(right_thread.load());
}

// --- cross-thread add/remove ---
//
// QTimer must live on the loop's thread; add/remove marshal onto it. These
// exercise the queued-create path and the remove-beats-create race.

VE_TEST(qt_loop_add_timer_from_worker_thread) {
    QtMainLoop lp(QCoreApplication::instance());

    std::atomic<int> ticks{0};
    std::atomic<Loop::TimerHandle> h{0};

    std::thread worker([&] { h.store(lp.addTimer(5, true, [&] { ticks.fetch_add(1); })); });
    worker.join();

    VE_ASSERT(h.load() != 0);
    VE_ASSERT(VE_QT_WAIT([&] { return ticks.load() >= 2; }));
    lp.removeTimer(h.load());
}

VE_TEST(qt_loop_remove_before_create_is_delivered) {
    QtMainLoop lp(QCoreApplication::instance());

    std::atomic<int> ticks{0};
    // The create is queued behind this very call stack, so the remove below
    // necessarily lands first — the timer must never start at all.
    auto h = lp.addTimer(2, true, [&] { ticks.fetch_add(1); });
    VE_ASSERT(lp.removeTimer(h));

    VE_QT_SETTLE(80);
    VE_ASSERT_EQ(ticks.load(), 0);
}

VE_TEST(qt_loop_remove_from_worker_thread) {
    QtMainLoop lp(QCoreApplication::instance());

    std::atomic<int> ticks{0};
    auto h = lp.addTimer(3, true, [&] { ticks.fetch_add(1); });
    VE_ASSERT(VE_QT_WAIT([&] { return ticks.load() >= 2; }));

    std::atomic<bool> removed{false};
    std::thread worker([&] { removed.store(lp.removeTimer(h)); });
    worker.join();
    VE_ASSERT(removed.load());

    const int settled = ticks.load();
    VE_QT_SETTLE();
    VE_ASSERT_EQ(ticks.load(), settled);
}

VE_TEST(qt_loop_concurrent_add_remove) {
    QtMainLoop lp(QCoreApplication::instance());

    std::atomic<int> ticks{0};
    std::vector<std::thread> threads;
    std::mutex hmtx;
    std::vector<Loop::TimerHandle> handles;

    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < 25; ++i) {
                auto h = lp.addTimer(1, true, [&] { ticks.fetch_add(1); });
                std::lock_guard<std::mutex> lk(hmtx);
                handles.push_back(h);
            }
        });
    }
    for (auto& th : threads) th.join();
    VE_ASSERT_EQ((int)handles.size(), 100);

    VE_QT_SETTLE(50);

    std::vector<std::thread> killers;
    for (int t = 0; t < 4; ++t) {
        killers.emplace_back([&, t] {
            for (size_t i = t; i < handles.size(); i += 4) lp.removeTimer(handles[i]);
        });
    }
    for (auto& th : killers) th.join();

    VE_QT_SETTLE();
    const int settled = ticks.load();
    VE_QT_SETTLE();
    VE_ASSERT_EQ(ticks.load(), settled);   // every timer is gone
}

VE_TEST(qt_loop_timers_die_with_loop) {
    std::atomic<int> ticks{0};
    {
        QtMainLoop lp(QCoreApplication::instance());
        lp.addTimer(2, true, [&] { ticks.fetch_add(1); });
        VE_ASSERT(VE_QT_WAIT([&] { return ticks.load() >= 2; }));
    }   // ~QtMainLoop → ~QtTimers → clear()

    const int settled = ticks.load();
    VE_QT_SETTLE();
    VE_ASSERT_EQ(ticks.load(), settled);
}

// --- Object timers on top of a Qt loop ---

VE_TEST(qt_object_timer_repeat) {
    QtMainLoop lp(QCoreApplication::instance());

    Object src("src"), obs("obs");
    std::atomic<int> ticks{0};
    std::atomic<int64_t> last{0};

    auto id = src.startTimer(5, true, &lp);
    VE_ASSERT(id >= Object::TIMER_BASE);
    VE_ASSERT(src.hasTimer(id));
    src.connect(id, &obs, [&](int64_t n) { last.store(n); ticks.fetch_add(1); });

    VE_ASSERT(VE_QT_WAIT([&] { return ticks.load() >= 3; }));
    VE_ASSERT_EQ(last.load(), (int64_t)ticks.load());

    VE_ASSERT(src.killTimer(id));
    VE_ASSERT(!src.hasTimer(id));

    const int settled = ticks.load();
    VE_QT_SETTLE();
    VE_ASSERT_EQ(ticks.load(), settled);
}

VE_TEST(qt_object_timer_single_shot) {
    QtMainLoop lp(QCoreApplication::instance());

    Object src("src"), obs("obs");
    std::atomic<int> ticks{0};

    auto id = src.startTimer(5, false, &lp);
    src.connect(id, &obs, [&]() { ticks.fetch_add(1); });

    VE_ASSERT(VE_QT_WAIT([&] { return ticks.load() == 1; }));
    VE_QT_SETTLE();
    VE_ASSERT_EQ(ticks.load(), 1);
    VE_ASSERT(!src.hasTimer(id));
}

VE_TEST(qt_object_timer_dies_with_owner) {
    QtMainLoop lp(QCoreApplication::instance());

    Object obs("obs");
    std::atomic<int> ticks{0};
    {
        Object src("src");
        auto id = src.startTimer(2, true, &lp);
        src.connect(id, &obs, [&]() { ticks.fetch_add(1); });
        VE_ASSERT(VE_QT_WAIT([&] { return ticks.load() >= 2; }));
    }

    const int settled = ticks.load();
    VE_QT_SETTLE();
    VE_ASSERT_EQ(ticks.load(), settled);
}

VE_TEST(qt_object_timer_kill_from_slot) {
    QtMainLoop lp(QCoreApplication::instance());

    Object src("src"), obs("obs");
    std::atomic<int> ticks{0};

    auto id = src.startTimer(2, true, &lp);
    src.connect(id, &obs, [&](int64_t) { ticks.fetch_add(1); src.killTimer(id); });

    VE_ASSERT(VE_QT_WAIT([&] { return ticks.load() == 1; }));
    VE_QT_SETTLE();
    VE_ASSERT_EQ(ticks.load(), 1);
}

VE_TEST(qt_loop_remove_from_own_tick) {
    QtMainLoop lp(QCoreApplication::instance());

    std::atomic<int> ticks{0};
    std::atomic<Loop::TimerHandle> h{0};
    std::atomic<bool> removed_self{false};

    h.store(lp.addTimer(2, true, [&] {
        ticks.fetch_add(1);
        removed_self.store(lp.removeTimer(h.load()));   // must not deadlock
    }));

    VE_ASSERT(VE_QT_WAIT([&] { return ticks.load() == 1; }));
    VE_ASSERT(removed_self.load());
    VE_QT_SETTLE();
    VE_ASSERT_EQ(ticks.load(), 1);
}

VE_TEST(qt_loop_throwing_tick_keeps_loop_alive) {
    QtMainLoop lp(QCoreApplication::instance());

    std::atomic<int> ticks{0};
    auto h = lp.addTimer(2, true, [&] {
        ticks.fetch_add(1);
        throw std::runtime_error("boom");
    });

    // Throwing through the Qt event loop is UB — the tick must swallow it.
    VE_ASSERT(VE_QT_WAIT([&] { return ticks.load() >= 2; }));
    lp.removeTimer(h);

    std::atomic<int> posted{0};
    lp.post([&] { posted.store(1); });
    VE_ASSERT(VE_QT_WAIT([&] { return posted.load() == 1; }));
}

VE_TEST(qt_loop_slow_tick_does_not_burst) {
    QtMainLoop lp(QCoreApplication::instance());

    std::atomic<int> ticks{0};
    auto h = lp.addTimer(2, true, [&] {
        ticks.fetch_add(1);
        QThread::msleep(20);
    });

    VE_ASSERT(VE_QT_WAIT([&] { return ticks.load() >= 2; }));
    VE_QT_SETTLE(100);
    lp.removeTimer(h);

    VE_ASSERT(ticks.load() < 20);   // QTimer does not queue up missed shots
}
