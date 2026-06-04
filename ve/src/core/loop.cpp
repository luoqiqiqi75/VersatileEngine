#include "ve/core/loop.h"

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <asio/io_context.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/post.hpp>

#include <atomic>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

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

// ============================================================================
// Built-in asio loops
// ============================================================================

using AsioWorkGuard = asio::executor_work_guard<asio::io_context::executor_type>;

struct AsioLoop::Private
{
    asio::io_context io;
    std::optional<AsioWorkGuard> guard;
    std::thread worker;
    std::atomic<bool> is_running{false};
    std::mutex mtx;

    Private()
        : io(1)
        , guard(asio::make_work_guard(io))
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

    _p->worker = std::thread([st = _p.get()] { st->io.run(); });
    return true;
}

bool AsioLoop::stop()
{
    std::lock_guard<std::mutex> lk(_p->mtx);
    if (!_p->is_running) return false;

    _p->is_running = false;
    _p->guard.reset();
    _p->io.stop();

    if (_p->worker.joinable()) _p->worker.join();
    return true;
}

bool AsioLoop::isRunning() const { return _p->is_running; }
size_t AsioLoop::processEvents() { return _p->io.poll(); }

struct AsioPoolLoop::Private
{
    asio::io_context io;
    std::optional<AsioWorkGuard> guard;
    std::vector<std::thread> workers;
    unsigned threads;
    std::atomic<bool> is_running{false};
    std::mutex mtx;

    explicit Private(unsigned n)
        : io(static_cast<int>(n ? n : 1))
        , guard(asio::make_work_guard(io))
        , threads(n ? n : 1)
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
        _p->workers.emplace_back([st = _p.get()] { st->io.run(); });
    return true;
}

bool AsioPoolLoop::stop()
{
    std::lock_guard<std::mutex> lk(_p->mtx);
    if (!_p->is_running) return false;

    _p->is_running = false;
    _p->guard.reset();
    _p->io.stop();

    for (auto& w : _p->workers)
        if (w.joinable()) w.join();
    _p->workers.clear();
    return true;
}

bool AsioPoolLoop::isRunning() const { return _p->is_running; }
size_t AsioPoolLoop::processEvents() { return _p->io.poll(); }

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

} // namespace ve
