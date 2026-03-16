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

/**
* @brief Object — minimal controllable entity with signal/slot
*
* Thread-safe signal connection and dispatch.
* Callbacks are invoked outside the lock (no deadlock in trigger).
*
* Dispatch priority for trigger():
*   1. per-connection LoopRef  (if set in connect())
*   2. global default LoopRef  (if set via loop::setDefault())
*   3. direct call             (zero-overhead, default for pure C++)
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
class VE_API Object
{
    // --- internal: callable → ActionT conversion ---
    template<typename T> struct _sig : _sig<decltype(&std::decay_t<T>::operator())> {};
    template<typename C, typename R, typename... A> struct _sig<R(C::*)(A...) const> { using types = std::tuple<A...>; };
    template<typename C, typename R, typename... A> struct _sig<R(C::*)(A...)>       { using types = std::tuple<A...>; };
    template<typename R, typename... A> struct _sig<R(*)(A...)>                       { using types = std::tuple<A...>; };
    template<typename R, typename... A> struct _sig<std::function<R(A...)>>           { using types = std::tuple<A...>; };

    template<typename Fn, typename... A, size_t... I>
    static void _call(const Fn& fn, const Var& v, std::tuple<A...>*, std::index_sequence<I...>) {
        if constexpr (sizeof...(A) == 0)
            fn();
        else if constexpr (sizeof...(A) == 1)
            fn(v.template as<std::tuple_element_t<0, std::tuple<A...>>>());
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

    enum Signal : SignalT { OBJECT_DELETED = 0xffff };

    bool hasConnection(SignalT signal, Object* observer);
    template<SignalT S> bool hasConnection(Object* observer) { return hasConnection(S, observer); }

    // --- connect ---
    void connect(SignalT signal, Object* observer, const ActionT& action, LoopRef loop = {});
    template<SignalT S> void connect(Object* observer, const ActionT& action, LoopRef loop = {})
    { connect(S, observer, action, loop); }

    // Typed connect (runtime signal): any callable → auto-wrap into ActionT
    //   connect(sig, obs, []() { ... })                   ignore data
    //   connect(sig, obs, [](int a) { ... })              single-arg unpack
    //   connect(sig, obs, [](int a, string b) { ... })    multi-arg unpack
    template<typename Fn,
        std::enable_if_t<!std::is_convertible_v<Fn, ActionT>, int> = 0>
    void connect(SignalT signal, Object* observer, Fn fn, LoopRef loop = {}) {
        using T = typename _sig<std::decay_t<Fn>>::types;
        connect(signal, observer, [fn](const Var& data) {
            _call(fn, data, static_cast<T*>(nullptr),
                  std::make_index_sequence<std::tuple_size_v<T>>{});
        }, loop);
    }

    // Typed connect (compile-time signal): any callable → auto-wrap into ActionT
    //   connect<S>(obs, [](int a, string b) { ... })     multi-arg unpack
    //   connect<S>(obs, [](Node* p) { ... })              single-arg unpack
    //   connect<S>(obs, []() { ... })                     ignore data
    template<SignalT S, typename Fn,
        std::enable_if_t<!std::is_convertible_v<Fn, ActionT>, int> = 0>
    void connect(Object* observer, Fn fn, LoopRef loop = {}) {
        using T = typename _sig<std::decay_t<Fn>>::types;
        connect(S, observer, [fn](const Var& data) {
            _call(fn, data, static_cast<T*>(nullptr),
                  std::make_index_sequence<std::tuple_size_v<T>>{});
        }, loop);
    }

    // --- trigger ---
    void trigger(SignalT signal, const Var& data = {});

    // 0-arg
    template<SignalT S> void trigger()
    { trigger(S); }

    // 1-arg (implicit Var conversion)
    template<SignalT S, typename Arg>
    void trigger(Arg&& arg)
    { trigger(S, Var(std::forward<Arg>(arg))); }

    // 2+ args → pack into Var list
    template<SignalT S, typename A1, typename A2, typename... Rest>
    void trigger(A1&& a1, A2&& a2, Rest&&... rest) {
        trigger(S, Var(Var::ListV{
            Var(std::forward<A1>(a1)),
            Var(std::forward<A2>(a2)),
            Var(std::forward<Rest>(rest))...
        }));
    }

    // --- disconnect ---
    void disconnect(SignalT signal, Object* observer);
    void disconnect(Object* observer);
    template<SignalT S> void disconnect(Object* observer) { disconnect(S, observer); }

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
