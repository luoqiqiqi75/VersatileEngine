// ----------------------------------------------------------------------------
// factory.h — Pool, Pooled, PoolPtr, SharedPoolPtr, Factory
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------

#pragma once

#include "object.h"

namespace ve {

// ============================================================================
// Pool<T> — fixed-size object pool, singleton per T
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
        Slot   slots[BlockCap];
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
        return &_head->slots[_used++];
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
// Pooled<T> — CRTP mixin: routes new/delete through Pool<T>
// ============================================================================
//
// Usage (in .cpp only):
//   struct Children : Pooled<Children> { ... };
//   auto* c = new Children();   // → Pool<Children>
//   delete c;                   // → Pool<Children>
//
template<typename T>
struct Pooled
{
    static void* operator new(size_t)      { return Pool<T>::instance().alloc(); }
    static void  operator delete(void* p)  { Pool<T>::instance().dealloc(p); }
};

// ============================================================================
// PoolPtr<T> — scoped pool pointer (like QScopedPointer)
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
// SharedPoolPtr<T> — ref-counted pool pointer (like std::shared_ptr)
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
// Factory<Signature> — named function registry with cache
// ============================================================================

template<class Signature>
class Factory : public Object, public Dict<std::function<Signature>>
{
public:
    using FunctionT = std::function<Signature>;
    using FnTraitsT = basic::FnTraits<FunctionT>;
    using RetT      = typename FnTraitsT::RetT;

public:
    explicit Factory(const std::string& name) : Object(name) {}
    virtual ~Factory() {}

    template<typename... Params>
    RetT exec(const std::string& key, Params&&... params) { return this->has(key) ? (*this)[key](std::forward<Params>(params)...) : (RetT)(NULL); }
    RetT exec(const std::string& key) { return this->has(key) ? (*this)[key]() : (RetT)(NULL); }

    template<typename T, typename... Params>
    T execAs(const std::string& key, Params&&... params) { return static_cast<T>(exec(key, std::forward<Params>(params)...)); }

    template<typename... Params>
    RetT produce(const std::string& key, Params&&... params) {
        RetT ret = exec(key, std::forward<Params>(params)...);
        _cache[key] = ret;
        return ret;
    }

    RetT instance(const std::string& key) { return _cache.value(key, NULL); }

private:
    Dict<RetT> _cache;
};

// ============================================================================
// ve::version — named version registry (Factory<int()>)
// ============================================================================

namespace version {

using Manager = Factory<int()>;
VE_API Manager& manager();
VE_API int  number(const std::string& key);
VE_API bool check(const std::string& key, int min_api);

} // namespace version

}

#define VE_REGISTER_VERSION(Key, Ver) \
    VE_AUTO_RUN(ve::version::manager().insertOne(#Key, [] () -> int { return Ver; });)
