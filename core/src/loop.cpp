#include "ve/core/loop.h"

// standalone asio (header-only, from deps/asio2/3rd)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <asio/io_context.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/post.hpp>

#include <optional>

namespace ve {

// ============================================================================
// AsioContext::Context — internal state
// ============================================================================

using AsioWorkGuard = asio::executor_work_guard<asio::io_context::executor_type>;

struct LoopTraits<AsioContext>::Context
{
    asio::io_context io;
    std::optional<AsioWorkGuard> guard;
    std::vector<std::thread> threads;
    int thread_count;
    std::atomic<bool> is_running{false};
    std::mutex mtx;   // protects start/stop

    explicit Context(int n)
        : io()
        , guard(asio::make_work_guard(io))
        , thread_count(std::max(n, 1))
    {}
};

// ============================================================================
// LoopTraits<AsioContext> — static functions
// ============================================================================

LoopTraits<AsioContext>::Context*
LoopTraits<AsioContext>::create(int threads)
{
    return new Context(threads);
}

void LoopTraits<AsioContext>::destroy(Context* ctx)
{
    if (!ctx) return;
    stop(ctx);
    delete ctx;
}

void LoopTraits<AsioContext>::post(Context* ctx, Task task)
{
    asio::post(ctx->io, std::move(task));
}

bool LoopTraits<AsioContext>::start(Context* ctx)
{
    std::lock_guard<std::mutex> lk(ctx->mtx);
    if (ctx->is_running) return false;

    ctx->io.restart();
    ctx->guard.emplace(asio::make_work_guard(ctx->io));
    ctx->is_running = true;

    for (int i = 0; i < ctx->thread_count; ++i) {
        ctx->threads.emplace_back([ctx] { ctx->io.run(); });
    }
    return true;
}

bool LoopTraits<AsioContext>::stop(Context* ctx)
{
    std::lock_guard<std::mutex> lk(ctx->mtx);
    if (!ctx->is_running) return false;

    ctx->is_running = false;
    ctx->guard.reset();     // drop work guard → io_context::run() returns when idle
    ctx->io.stop();         // interrupt immediately

    for (auto& t : ctx->threads) {
        if (t.joinable()) t.join();
    }
    ctx->threads.clear();
    return true;
}

bool LoopTraits<AsioContext>::running(const Context* ctx)
{
    return ctx->is_running;
}

// ============================================================================
// loop:: — default loop for Object signal dispatch
// ============================================================================

static std::atomic<LoopRef*> s_default_loop{nullptr};

void loop::setDefault(LoopRef ref)
{
    // Intentional leak — the LoopRef is small, lives for program lifetime
    auto* p = new LoopRef(std::move(ref));
    auto* old = s_default_loop.exchange(p, std::memory_order_acq_rel);
    delete old;
}

LoopRef loop::defaultLoop()
{
    auto* p = s_default_loop.load(std::memory_order_acquire);
    return p ? *p : LoopRef{};
}

// ============================================================================
// loop:: — global loop singletons
// ============================================================================
//
// Uses intentional-leak pattern (new without delete) to avoid
// static destruction order issues with global/static objects.

EventLoop& loop::main()
{
    static auto* s = [] {
        auto* l = new EventLoop("ve.loop.main", 1);
        l->start();
        return l;
    }();
    return *s;
}

EventLoop& loop::pool(int threads)
{
    static auto* s = [threads] {
        auto* l = new EventLoop("ve.loop.pool", threads);
        l->start();
        return l;
    }();
    return *s;
}

} // namespace ve
