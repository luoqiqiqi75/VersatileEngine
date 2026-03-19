// ----------------------------------------------------------------------------
// object.h — ve::Object (signal/slot, thread-safe)
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------

#pragma once

#include "base.h"
#include "var.h"
#include "loop.h"

constexpr const char* VE_UNDEFINED_OBJECT_NAME = "@undefined";

namespace ve {

struct ObjectData
{
protected:
    int _flags = 0;
};

/**
* @brief Object — minimal controllable entity with signal/slot
*
* Thread-safe signal connection and dispatch.
* Callbacks are invoked outside the lock (no deadlock in trigger).
*
* Dispatch priority for trigger():
*   1. per-connection LoopRef  (if set in connect())
*   2. direct call             (zero-overhead, default for pure C++)
*
* Signal data passing (Godot-like):
*   trigger<S>()                       → Var()        (null, no data)
*   trigger<S>(arg)                    → Var(arg)     (single value)
*   trigger<S>(arg1, arg2, ...)        → Var(ListV)   (packed as list)
*
*   connect<S>(obs, [](const Var& v) { ... })          → raw Var callback
*   connect<S>(obs, [](int a, string b) { ... })       → auto-unpack from Var
*   connect<S>(obs, []() { ... })                      → ignore data
*/
struct VE_API Object : public ObjectData
{
    // --- internal: Var → typed args dispatch (uses basic::FnTraits) ---
    //
    // Qt-like partial-arg matching:
    //   Slot may accept fewer parameters than trigger sends.
    //   - 0 args  → ignore data
    //   - 1 arg   → if data is a List (multi-arg trigger), unpack data[0];
    //                otherwise use data directly
    //   - N args   → unpack data[0]..data[N-1] from the List (extra args ignored)
    //
    template<typename Fn, typename... A, size_t... I>
    static void _call(const Fn& fn, const Var& v, std::tuple<A...>*, std::index_sequence<I...>) {
        if constexpr (sizeof...(A) == 0)
            fn();
        else if constexpr (sizeof...(A) == 1) {
            using T0 = std::tuple_element_t<0, std::tuple<A...>>;
            if (v.isList())
                fn(v[0].template as<T0>());
            else
                fn(v.template as<T0>());
        }
        else
            fn(v[I].template as<std::tuple_element_t<I, std::tuple<A...>>>()...);
    }

public:
    using SignalT = int;
    using ActionT = std::function<void(const Var&)>;

    using MutexT = std::recursive_mutex;
    using LockT = std::lock_guard<MutexT>;

    explicit Object(const std::string& name = "");
    ~Object();

    const std::string& name() const;
    MutexT& mutex() const;

    enum ObjectSignal : SignalT {
        OBJECT_DELETED = 0xffff  // () — emitted in destructor, no data
    };

    enum ObjectFlag : int {
        SILENT = 0x01,  // suppress signal emission (except OBJECT_DELETED)
    };

    bool isSilent() const { return flags::get(_flags, SILENT); }
    void silent(bool on) { flags::set(_flags, SILENT, on); }

    bool hasConnection(SignalT signal);
    bool hasConnection(SignalT signal, Object* observer);
    template<SignalT S> bool hasConnection() { return hasConnection(S); }
    template<SignalT S> bool hasConnection(Object* observer) { return hasConnection(S, observer); }

    // --- connect (compile-time signal) ---
    template<SignalT S> void connect(Object* observer, const ActionT& action, LoopRef loop = {})
    { connect(S, observer, action, loop); }

    // Typed connect (runtime signal): any callable → auto-wrap into ActionT
    //   connect(sig, obs, []() { ... })                   ignore data
    //   connect(sig, obs, [](int a) { ... })              single-arg unpack
    //   connect(sig, obs, [](int a, string b) { ... })    multi-arg unpack
    template<typename Fn,
        std::enable_if_t<!std::is_convertible_v<Fn, ActionT>, int> = 0>
    void connect(SignalT signal, Object* observer, Fn fn, LoopRef loop = {}) {
        using Tuple = typename basic::FnTraits<std::decay_t<Fn>>::ArgsTuple;
        connect(signal, observer, [fn](const Var& data) {
            _call(fn, data, static_cast<Tuple*>(nullptr),
                  std::make_index_sequence<std::tuple_size_v<Tuple>>{});
        }, loop);
    }

    // Typed connect (compile-time signal): any callable → auto-wrap into ActionT
    //   connect<S>(obs, [](int a, string b) { ... })     multi-arg unpack
    //   connect<S>(obs, [](Node* p) { ... })              single-arg unpack
    //   connect<S>(obs, []() { ... })                     ignore data
    template<SignalT S, typename Fn,
        std::enable_if_t<!std::is_convertible_v<Fn, ActionT>, int> = 0>
    void connect(Object* observer, Fn fn, LoopRef loop = {}) {
        using Tuple = typename basic::FnTraits<std::decay_t<Fn>>::ArgsTuple;
        connect(S, observer, [fn](const Var& data) {
            _call(fn, data, static_cast<Tuple*>(nullptr),
                  std::make_index_sequence<std::tuple_size_v<Tuple>>{});
        }, loop);
    }

    // --- trigger (compile-time signal, hasConnection check avoids Var construction) ---

    // 0-arg
    template<SignalT S> void trigger() {
        if (isSilent() || !hasConnection(S)) return;
        trigger(S);
    }

    // 1-arg (implicit Var conversion)
    template<SignalT S, typename Arg>
    void trigger(Arg&& arg) {
        if (isSilent() || !hasConnection(S)) return;
        trigger(S, Var(std::forward<Arg>(arg)));
    }

    // 2+ args → pack into Var list
    template<SignalT S, typename A1, typename A2, typename... Rest>
    void trigger(A1&& a1, A2&& a2, Rest&&... rest) {
        if (isSilent() || !hasConnection(S)) return;
        trigger(S, Var(Var::ListV{
            Var(std::forward<A1>(a1)),
            Var(std::forward<A2>(a2)),
            Var(std::forward<Rest>(rest))...
        }));
    }

    // --- once (single-shot connect, auto-disconnects after first trigger) ---

    template<SignalT S> void once(Object* observer, const ActionT& action, LoopRef loop = {})
    { once(S, observer, action, loop); }

    template<SignalT S, typename Fn,
        std::enable_if_t<!std::is_convertible_v<Fn, ActionT>, int> = 0>
    void once(Object* observer, Fn fn, LoopRef loop = {}) {
        using Tuple = typename basic::FnTraits<std::decay_t<Fn>>::ArgsTuple;
        once(S, observer, [fn](const Var& data) {
            _call(fn, data, static_cast<Tuple*>(nullptr),
                  std::make_index_sequence<std::tuple_size_v<Tuple>>{});
        }, loop);
    }

    // --- disconnect ---
    void disconnect(SignalT signal, Object* observer);
    void disconnect(Object* observer);
    template<SignalT S> void disconnect(Object* observer) { disconnect(S, observer); }

protected:
    // Runtime-signal connect/trigger — prefer compile-time template versions above.
    // Protected so derived classes (e.g. Node::activate) can still use them directly.
    void connect(SignalT signal, Object* observer, const ActionT& action, LoopRef loop = {});
    void once(SignalT signal, Object* observer, const ActionT& action, LoopRef loop = {});
    void trigger(SignalT signal, const Var& data = {});

private:
    VE_DECLARE_UNIQUE_PRIVATE
};

/**
* @brief Manager Class is a convenient object container
*/
class VE_API Manager : public Object, public Hash<Object*>
{
public:
    explicit Manager(const std::string& name);
    virtual ~Manager();

    Object* add(Object* obj, bool delete_if_failed = false);
    template<typename SubObj> std::enable_if_t<std::is_base_of_v<Object, SubObj>, SubObj*> add(SubObj* obj, bool delete_if_failed = false)
    { return add(static_cast<Object *>(obj), delete_if_failed) ? obj : nullptr; }

    bool remove(Object* obj, bool auto_delete = true);
    bool remove(const std::string& name, bool auto_delete = true);

    Object* get(const std::string& key) const;
    template<class SubObj> std::enable_if_t<std::is_base_of_v<Object, SubObj>, SubObj*> get(const std::string& key) const
    { return static_cast<SubObj*>(get(key)); }
};

} // namespace ve
