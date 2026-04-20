// ----------------------------------------------------------------------------
// factory.h - Pool, Pooled, PoolPtr, SharedPoolPtr, Factory
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------

#pragma once

#include "node.h"
#include "loop.h"

namespace ve {

// ============================================================================
// Pool<T> - fixed-size object pool, singleton per T
// ============================================================================
//
// Bump-allocates from large blocks, recycles via free list.
// instance() uses intentional leak (never-destroyed singleton) to avoid
// static destruction order issues with global/static objects.
//
template<typename T, size_t BlockCap = 512>
class Pool
{
    union Slot {
        alignas(T) char buf[sizeof(T)];
        Slot* next;
    };
    struct Block {
        Slot   cells[BlockCap];
        Block* next = nullptr;
    };

    Slot*   _free = nullptr;
    Block*  _head = nullptr;
    size_t  _used = BlockCap;   // force first Block allocation

public:
    static Pool& instance() { static auto* s = new Pool(); return *s; }

    void* alloc()
    {
        if (_free) {
            auto* p = _free;
            _free = _free->next;
            return p;
        }
        if (_used >= BlockCap) {
            auto* b = new Block();
            b->next = _head;
            _head = b;
            _used = 0;
        }
        return &_head->cells[_used++];
    }

    void dealloc(void* p)
    {
        auto* s = static_cast<Slot*>(p);
        s->next = _free;
        _free = s;
    }

    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

private:
    Pool() = default;
};

// ============================================================================
// Pooled<T> - CRTP mixin: routes new/delete through Pool<T>
// ============================================================================
//
// Usage (in .cpp only):
//   struct Children : Pooled<Children> { ... };
//   auto* c = new Children();   // -> Pool<Children>
//   delete c;                   // -> Pool<Children>
//
template<typename T>
struct Pooled
{
    static void* operator new(size_t)      { return Pool<T>::instance().alloc(); }
    static void  operator delete(void* p)  { Pool<T>::instance().dealloc(p); }
};

// ============================================================================
// PoolPtr<T> - scoped pool pointer (like QScopedPointer)
// ============================================================================
//
// Auto-creates T() from Pool<T> on construction.
// Auto-destroys and returns to Pool<T> on destruction.
// Non-copyable, non-movable (scoped ownership).
//
// Designed for PIMPL:
//   class Foo { VE_DECLARE_POOL_PRIVATE };   // header
//   struct Foo::Private { ... };              // .cpp
//   Foo::Foo() {}    // _p auto-created
//   Foo::~Foo() {}   // _p auto-destroyed
//
template<typename T>
class PoolPtr
{
    T* _ptr;

public:
    PoolPtr() : _ptr(new (Pool<T>::instance().alloc()) T()) {}

    ~PoolPtr() {
        if (_ptr) {
            _ptr->~T();
            Pool<T>::instance().dealloc(_ptr);
        }
    }

    T* operator->() const { return _ptr; }
    T& operator*()  const { return *_ptr; }
    T* get()         const { return _ptr; }

    PoolPtr(const PoolPtr&) = delete;
    PoolPtr& operator=(const PoolPtr&) = delete;
    PoolPtr(PoolPtr&&) = delete;
    PoolPtr& operator=(PoolPtr&&) = delete;
};

// ============================================================================
// SharedPoolPtr<T> - ref-counted pool pointer (like std::shared_ptr)
// ============================================================================
//
// Co-allocates ref count + T in a single Pool block.
// Copyable (ref count), movable.
//
template<typename T>
class SharedPoolPtr
{
    struct Control {
        std::atomic<int> ref{1};
        alignas(T) char  buf[sizeof(T)];
        T* ptr() { return reinterpret_cast<T*>(buf); }
    };

    Control* _cb = nullptr;

    static Pool<Control>& pool() { static auto* s = new Pool<Control>(); return *s; }

    void addRef()  { if (_cb) _cb->ref.fetch_add(1, std::memory_order_relaxed); }
    void release() {
        if (_cb && _cb->ref.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            _cb->ptr()->~T();
            pool().dealloc(_cb);
        }
        _cb = nullptr;
    }

public:
    SharedPoolPtr() {
        _cb = static_cast<Control*>(pool().alloc());
        _cb->ref.store(1, std::memory_order_relaxed);
        new (_cb->buf) T();
    }

    ~SharedPoolPtr() { release(); }

    SharedPoolPtr(const SharedPoolPtr& o) : _cb(o._cb) { addRef(); }
    SharedPoolPtr& operator=(const SharedPoolPtr& o) {
        if (this != &o) { release(); _cb = o._cb; addRef(); }
        return *this;
    }

    SharedPoolPtr(SharedPoolPtr&& o) noexcept : _cb(o._cb) { o._cb = nullptr; }
    SharedPoolPtr& operator=(SharedPoolPtr&& o) noexcept {
        if (this != &o) { release(); _cb = o._cb; o._cb = nullptr; }
        return *this;
    }

    T* operator->() const { return _cb->ptr(); }
    T& operator*()  const { return *_cb->ptr(); }
    T* get()         const { return _cb ? _cb->ptr() : nullptr; }
};

// --- PIMPL macros ---
#define VE_DECLARE_POOL_PRIVATE        struct Private; ve::PoolPtr<Private> _p;
#define VE_DECLARE_SHARED_POOL_PRIVATE struct Private; ve::SharedPoolPtr<Private> _p;

// ============================================================================
// Factory - Node-based named callable registry
// ============================================================================
//
// Each Factory instance owns a subtree under /ve/factory/{name}/.
// Registered items are child nodes whose value is Var::CALLABLE.
// Metadata hangs as child nodes: help, loop, declare/.
//
// Key format: slash-separated path, e.g. "ros/topic/list"
// Dots in keys are automatically converted to slashes.
//
class VE_API Factory : public Object
{
    VE_DECLARE_UNIQUE_PRIVATE

public:
    explicit Factory(const std::string& name);
    ~Factory();

    // Register a callable under key. Dots converted to slashes.
    void reg(const std::string& key, Var callable,
             const std::string& help = {}, LoopRef lr = {});

    // Look up the node for key (nullptr if not found).
    Node* node(const std::string& key) const;

    // Ensure node exists for key (creates if missing).
    Node* ensureNode(const std::string& key);

    // Remove the node for key (and its subtree).
    void erase(const std::string& key);

    // The root node of this factory (/ve/factory/{name}).
    Node* root() const;

    // All registered keys in this factory.
    Strings keys() const;

    // Typed call: pack params into Var, invoke, unpack result.
    // Pointer params are cast to void*; pointer return types use toPointer().
    // Fallback: RetT{} when key not found.
    template<typename RetT = Var, typename... Params>
    RetT exec(const std::string& key, Params&&... params) const
    {
        auto* nd = node(key);
        if (!nd || !nd->get().isCallable()) return RetT{};
        Var result = nd->get().invoke(_packArgs(std::forward<Params>(params)...));
        return result.as<RetT>();
    }

private:
    template<typename T>
    static Var _toVar(T&& v)
    {
        using DT = std::decay_t<T>;
        if constexpr (basic::Meta<DT>::is_raw_pointer)
            return Var(static_cast<void*>(v));
        else
            return Var(std::forward<T>(v));
    }

    template<typename... Params>
    static Var _packArgs(Params&&... params)
    {
        if constexpr (sizeof...(Params) == 0) {
            return Var{};
        } else if constexpr (sizeof...(Params) == 1) {
            Var _arr[] = { _toVar(std::forward<Params>(params))... };
            return _arr[0];
        } else {
            return Var(Var::ListV{_toVar(std::forward<Params>(params))...});
        }
    }
};

// ============================================================================
// ve::factory - global factory registry
// ============================================================================
//
// All factories live under /ve/factory/ in the global node tree.
// Each named factory (e.g. "cmd", "module") is a child node there.
//

namespace factory {

// Get or create a named factory (singleton per name).
VE_API Factory& get(const std::string& name);

// Root node /ve/factory.
VE_API Node* root();

// Enumerate registered keys under /ve/factory/{factory_name}/.
// A node is a registered entry if its value is CALLABLE or it has a "steps" child.
// Known metadata names (help, loop, declare, steps, priority, version, instance)
// are not traversed.
VE_API Strings keys(const std::string& factory_name);

} // namespace factory

// ============================================================================
// ve::version - named version registry (stored under /ve/factory/version/)
// ============================================================================

namespace version {

VE_API void reg(const std::string& key, int ver);
VE_API int  number(const std::string& key);
VE_API bool check(const std::string& key, int min_api);

} // namespace version

}

#define VE_REGISTER_VERSION(Key, Ver) \
    VE_AUTO_RUN(ve::version::reg(#Key, Ver);)
