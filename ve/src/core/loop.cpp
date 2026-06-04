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
    : _name(name)
{}

Loop::~Loop() = default;

const std::string& Loop::name() const { return _name; }

void Loop::post(Task task)
{
    if (task) task();
}

bool Loop::start() { return false; }
bool Loop::stop() { return false; }
bool Loop::isRunning() const { return false; }
bool Loop::isCurrentThread() const { return false; }
size_t Loop::processEvents() { return 0; }

// ============================================================================
// Built-in asio loops
// ============================================================================

namespace {

using AsioWorkGuard = asio::executor_work_guard<asio::io_context::executor_type>;

class AsioLoop : public Loop
{
    struct State {
        asio::io_context io;
        std::optional<AsioWorkGuard> guard;
        std::thread worker;
        std::atomic<bool> is_running{false};
        std::mutex mtx;

        State()
            : io(1)
            , guard(asio::make_work_guard(io))
        {}
    };

    mutable State _s;

public:
    explicit AsioLoop(const std::string& name)
        : Loop(name)
    {}

    ~AsioLoop() override { stop(); }

    void post(Task task) override
    {
        if (task) asio::post(_s.io, std::move(task));
    }

    bool start() override
    {
        std::lock_guard<std::mutex> lk(_s.mtx);
        if (_s.is_running) return false;

        _s.io.restart();
        _s.guard.emplace(asio::make_work_guard(_s.io));
        _s.is_running = true;

        State* st = &_s;
        _s.worker = std::thread([st]() { st->io.run(); });
        return true;
    }

    bool stop() override
    {
        std::lock_guard<std::mutex> lk(_s.mtx);
        if (!_s.is_running) return false;

        _s.is_running = false;
        _s.guard.reset();
        _s.io.stop();

        if (_s.worker.joinable()) _s.worker.join();
        return true;
    }

    bool isRunning() const override { return _s.is_running; }
    bool isCurrentThread() const override { return _s.io.get_executor().running_in_this_thread(); }
    size_t processEvents() override { return _s.io.poll(); }
};

class AsioPoolLoop : public Loop
{
    struct State {
        asio::io_context io;
        std::optional<AsioWorkGuard> guard;
        std::vector<std::thread> workers;
        unsigned threads;
        std::atomic<bool> is_running{false};
        std::mutex mtx;

        explicit State(unsigned n)
            : io(static_cast<int>(n ? n : 1))
            , guard(asio::make_work_guard(io))
            , threads(n ? n : 1)
        {}
    };

    mutable State _s;

public:
    explicit AsioPoolLoop(const std::string& name, unsigned threads = 4)
        : Loop(name)
        , _s(threads)
    {}

    ~AsioPoolLoop() override { stop(); }

    void post(Task task) override
    {
        if (task) asio::post(_s.io, std::move(task));
    }

    bool start() override
    {
        std::lock_guard<std::mutex> lk(_s.mtx);
        if (_s.is_running) return false;

        _s.io.restart();
        _s.guard.emplace(asio::make_work_guard(_s.io));
        _s.is_running = true;

        State* st = &_s;
        _s.workers.reserve(st->threads);
        for (unsigned i = 0; i < st->threads; ++i)
            _s.workers.emplace_back([st]() { st->io.run(); });
        return true;
    }

    bool stop() override
    {
        std::lock_guard<std::mutex> lk(_s.mtx);
        if (!_s.is_running) return false;

        _s.is_running = false;
        _s.guard.reset();
        _s.io.stop();

        for (auto& w : _s.workers)
            if (w.joinable()) w.join();
        _s.workers.clear();
        return true;
    }

    bool isRunning() const override { return _s.is_running; }
    bool isCurrentThread() const override { return _s.io.get_executor().running_in_this_thread(); }
    size_t processEvents() override { return _s.io.poll(); }
};

} // namespace

Loop* loop::main()
{
    static auto* s = [] {
        auto* l = new AsioLoop("main");
        l->start();
        return l;
    }();
    return s;
}

Loop* loop::pool()
{
    static auto* s = [] {
        auto* l = new AsioPoolLoop("pool", 4);
        l->start();
        return l;
    }();
    return s;
}

} // namespace ve
