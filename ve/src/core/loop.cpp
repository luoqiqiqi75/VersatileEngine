#include "ve/core/loop.h"
#include "ve/core/log.h"

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <asio/io_context.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/post.hpp>
#include <asio/signal_set.hpp>
#include <asio/steady_timer.hpp>
#include <asio/strand.hpp>

#include <csignal>
#include <exception>
#include <optional>

namespace ve {

// ============================================================================
// Loop
// ============================================================================

Loop::Loop(const std::string& name)
    : Entity(name)
{}

Loop::~Loop() = default;

void Loop::post(Task task)
{
    if (task) task();
}

bool Loop::start() { return false; }
bool Loop::stop() { return false; }
bool Loop::isRunning() const { return false; }
size_t Loop::processEvents() { return 0; }

Loop::TimerHandle Loop::addTimer(uint64_t, bool, Task) { return 0; }
bool Loop::removeTimer(TimerHandle) { return false; }

int Loop::exec()
{
    while (isRunning() && !_quit.load(std::memory_order_acquire)) {
        if (processEvents() == 0) std::this_thread::yield();
    }
    _quit.store(false, std::memory_order_release);   // consumed; allow re-exec
    return _exit_code.load(std::memory_order_acquire);
}

void Loop::quit(int exit_code)
{
    _exit_code.store(exit_code, std::memory_order_release);
    _quit.store(true, std::memory_order_release);
}

// ============================================================================
// Built-in asio loops
// ============================================================================

using AsioWorkGuard = asio::executor_work_guard<asio::io_context::executor_type>;

namespace {

// Timer backend shared by both asio loops. Owns nothing but the schedule —
// tick ownership and signal emission belong to Object.
//
// asio::steady_timer is not thread-safe, but add/remove may be called from any
// thread. Each timer therefore gets its own strand and every operation on it
// (arm, fire, cancel) is posted there. A synchronous `cancelled` flag makes
// removal take effect immediately, before the posted cancel is even dequeued —
// which is also what makes stop() final: handlers surviving a restart see it.
//
// Per-timer strands (rather than one shared one) keep a slow tick on a pool loop
// from delaying unrelated timers.
struct AsioTimers
{
    using Clock  = std::chrono::steady_clock;
    using Handle = Loop::TimerHandle;

    struct Rec {
        asio::steady_timer timer;
        Task               tick;
        Clock::duration    interval{};
        Clock::time_point  deadline{};   // strand-only
        bool               repeat = true;
        std::atomic<bool>  cancelled{false};

        Rec(asio::io_context& io, Task t, uint64_t ms, bool rep)
            : timer(asio::make_strand(io))
            , tick(std::move(t))
            , interval(std::chrono::milliseconds(ms))
            , deadline(Clock::now() + std::chrono::milliseconds(ms))
            , repeat(rep)
        {}

        bool dead() const { return cancelled.load(std::memory_order_acquire); }
    };
    using RecPtr = std::shared_ptr<Rec>;

    asio::io_context& io;
    std::atomic<Handle> seq{0};

    mutable std::mutex mtx;
    UnorderedHashMap<Handle, RecPtr> recs;

    explicit AsioTimers(asio::io_context& ctx) : io(ctx) {}
    ~AsioTimers() { clear(); }

    Handle start(uint64_t ms, bool repeat, Task tick)
    {
        if (!tick) return 0;

        const Handle h = seq.fetch_add(1, std::memory_order_relaxed) + 1;
        auto rec = std::make_shared<Rec>(io, std::move(tick), ms, repeat);
        {
            std::lock_guard<std::mutex> lk(mtx);
            recs[h] = rec;
        }
        asio::post(rec->timer.get_executor(), [this, h, rec] { arm(h, rec); });
        return h;
    }

    // --- strand-only below ---

    void arm(Handle h, const RecPtr& rec)
    {
        if (rec->dead()) return;
        rec->timer.expires_at(rec->deadline);
        rec->timer.async_wait([this, h, rec](const asio::error_code& ec) {
            if (!ec) fire(h, rec);
        });
    }

    void fire(Handle h, const RecPtr& rec)
    {
        if (rec->dead()) return;
        if (!rec->repeat) forget(h);   // before the tick, so removeTimer() is honest inside it

        // Throwing out of here would escape into io.run() and kill the thread.
        try {
            rec->tick();
        } catch (const std::exception& e) {
            veLogE << "timer tick threw:" << e.what();
        } catch (...) {
            veLogE << "timer tick threw";
        }

        if (!rec->repeat || rec->dead()) return;   // may have been removed by its own tick

        const auto now = Clock::now();
        rec->deadline += rec->interval;
        // A tick slower than the interval must not burst to catch up.
        if (rec->deadline <= now) rec->deadline = now + rec->interval;
        arm(h, rec);
    }

    // --- any thread ---

    bool remove(Handle h)
    {
        RecPtr rec;
        {
            std::lock_guard<std::mutex> lk(mtx);
            auto it = recs.find(h);
            if (it == recs.end()) return false;
            rec = std::move(it->second);
            recs.erase(it);
        }
        kill(rec);
        return true;
    }

    void clear()
    {
        UnorderedHashMap<Handle, RecPtr> dead;
        {
            std::lock_guard<std::mutex> lk(mtx);
            dead.swap(recs);
        }
        for (auto& kv : dead) kill(kv.second);
    }

private:
    // Cancel takes effect on the flag immediately; the timer object itself is
    // only ever touched on its strand.
    static void kill(const RecPtr& rec)
    {
        rec->cancelled.store(true, std::memory_order_release);
        asio::post(rec->timer.get_executor(), [rec] { rec->timer.cancel(); });
    }

    void forget(Handle h)
    {
        std::lock_guard<std::mutex> lk(mtx);
        recs.erase(h);
    }
};

} // namespace

struct AsioLoop::Private
{
    asio::io_context io;
    std::optional<AsioWorkGuard> guard;
    std::thread worker;
    std::atomic<bool> is_running{false};
    std::mutex mtx;
    AsioTimers timers;

    explicit Private(asio::io_context::count_type n = 1)
        : io(static_cast<int>(n))
        , guard(asio::make_work_guard(io))
        , timers(io)
    {}
};

AsioLoop::AsioLoop(const std::string& name) : Loop(name), _p(std::make_unique<Private>())
{}

AsioLoop::~AsioLoop()
{
    stop();
}

void AsioLoop::post(Task task)
{
    if (task) asio::post(_p->io, std::move(task));
}

bool AsioLoop::start()
{
    std::lock_guard<std::mutex> lk(_p->mtx);
    if (_p->is_running) return false;

    _p->io.restart();
    _p->guard.emplace(asio::make_work_guard(_p->io));
    _p->is_running = true;

    _p->worker = std::thread([st = _p.get(), self = this] {
        loop::setCurrent(self);
        st->io.run();
    });
    return true;
}

bool AsioLoop::stop()
{
    std::lock_guard<std::mutex> lk(_p->mtx);
    if (!_p->is_running) return false;

    _p->timers.clear();   // timers do not outlive the scheduler
    _p->is_running = false;
    _p->guard.reset();
    _p->io.stop();

    if (_p->worker.joinable()) _p->worker.join();
    return true;
}

Loop::TimerHandle AsioLoop::addTimer(uint64_t ms, bool repeat, Task tick)
{
    return _p->timers.start(ms, repeat, std::move(tick));
}

bool AsioLoop::removeTimer(TimerHandle handle) { return _p->timers.remove(handle); }

bool AsioLoop::isRunning() const { return _p->is_running; }

size_t AsioLoop::processEvents()
{
    Loop* prev = loop::current();
    loop::setCurrent(this);
    size_t n = _p->io.poll();
    loop::setCurrent(prev);
    return n;
}

int AsioLoop::exec()
  {
      asio::signal_set signals(_p->io, SIGINT, SIGTERM);
      signals.async_wait([this](const asio::error_code& ec, int) {
          if (!ec) quit(0);
      });

      while (isRunning() && !_quit.load(std::memory_order_acquire)) {
          if (processEvents() == 0) std::this_thread::yield();
      }
      _quit.store(false, std::memory_order_release);
      return _exit_code.load(std::memory_order_acquire);
  }

struct AsioPoolLoop::Private
{
    asio::io_context io;
    std::optional<AsioWorkGuard> guard;
    std::vector<std::thread> workers;
    unsigned threads;
    std::atomic<bool> is_running{false};
    std::mutex mtx;
    AsioTimers timers;

    Private(unsigned n)
        : io(static_cast<int>(n ? n : 1))
        , guard(asio::make_work_guard(io))
        , threads(n ? n : 1)
        , timers(io)
    {}
};

AsioPoolLoop::AsioPoolLoop(const std::string& name, unsigned threads) : Loop(name), _p(std::make_unique<Private>(threads))
{}

AsioPoolLoop::~AsioPoolLoop()
{
    stop();
}

void AsioPoolLoop::post(Task task)
{
    if (task) asio::post(_p->io, std::move(task));
}

bool AsioPoolLoop::start()
{
    std::lock_guard<std::mutex> lk(_p->mtx);
    if (_p->is_running) return false;

    _p->io.restart();
    _p->guard.emplace(asio::make_work_guard(_p->io));
    _p->is_running = true;

    _p->workers.reserve(_p->threads);
    for (unsigned i = 0; i < _p->threads; ++i)
        _p->workers.emplace_back([st = _p.get(), self = this] {
            loop::setCurrent(self);
            st->io.run();
        });
    return true;
}

bool AsioPoolLoop::stop()
{
    std::lock_guard<std::mutex> lk(_p->mtx);
    if (!_p->is_running) return false;

    _p->timers.clear();
    _p->is_running = false;
    _p->guard.reset();
    _p->io.stop();

    for (auto& w : _p->workers)
        if (w.joinable()) w.join();
    _p->workers.clear();
    return true;
}

Loop::TimerHandle AsioPoolLoop::addTimer(uint64_t ms, bool repeat, Task tick)
{
    return _p->timers.start(ms, repeat, std::move(tick));
}

bool AsioPoolLoop::removeTimer(TimerHandle handle) { return _p->timers.remove(handle); }

bool AsioPoolLoop::isRunning() const { return _p->is_running; }
size_t AsioPoolLoop::processEvents()
{
    Loop* prev = loop::current();
    loop::setCurrent(this);
    size_t n = _p->io.poll();
    loop::setCurrent(prev);
    return n;
}

namespace {

struct CoreLoops
{
    AsioLoop* default_main = nullptr;
    AsioPoolLoop* default_pool = nullptr;
    Loop* main = nullptr;
    Loop* pool = nullptr;

    Loop* mainLoop()
    {
        if (main) return main;
        if (!default_main) {
            default_main = new AsioLoop("main");
            default_main->start();
        }
        return default_main;
    }

    Loop* poolLoop()
    {
        if (pool) return pool;
        if (!default_pool) {
            default_pool = new AsioPoolLoop("pool", 4);
            default_pool->start();
        }
        return default_pool;
    }
};

CoreLoops& coreLoops()
{
    static CoreLoops s;
    return s;
}

thread_local Loop* t_currentLoop = nullptr;

} // namespace

Loop* loop::main() { return coreLoops().mainLoop(); }
Loop* loop::pool() { return coreLoops().poolLoop(); }

void loop::setMain(Loop* loop)
{
    coreLoops().main = loop;
}

void loop::setPool(Loop* loop)
{
    coreLoops().pool = loop;
}

Loop* loop::current() { return t_currentLoop; }
void  loop::setCurrent(Loop* loop) { t_currentLoop = loop; }

} // namespace ve
