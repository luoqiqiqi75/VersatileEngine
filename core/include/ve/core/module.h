// ----------------------------------------------------------------------------
// module.h - Module lifecycle
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------
//
// Lifecycle (driven by ve::entry or manual):
//   all constructors (by priority) → all init → all ready
//                                  → all deinit → all destructors
//
// Convention:
//   constructor:  read node() config, create own resources (no cross-module deps)
//   init():       data tree setup, register commands
//   ready():      subscribe to other modules' data (cross-module dependencies)
//   deinit():     reverse of ready - disconnect cross-module subscriptions
//   destructor:   release own resources (reverse of constructor)
//
// ----------------------------------------------------------------------------

#pragma once

#include "node.h"

namespace ve {

enum ModuleSignal : Object::SignalT {
    MODULE_STATE_ABOUT_TO_CHANGE    = 0xFFFF'FFE0,
    MODULE_STATE_CHANGED            = 0xFFFF'FFE1
};

class VE_API Module : public Object
{
    VE_DECLARE_PRIVATE

public:
    enum State : int {
        NONE    = 0x0f00 | 0x00,
        INIT    = 0x0f00 | 0x10,
        READY   = 0x0f00 | 0x80,
        DEINIT  = 0x0f00 | 0xf0
    };

public:
    explicit Module(const std::string& name);
    ~Module();

    State state() const;

    const std::string& key() const { return name(); }

    /// Module workspace node at ve/entry/module/{name}
    Node* node() const;

    template<State s> void exeState();

protected:
    virtual void init();
    virtual void ready();
    virtual void deinit();
};

template<class T>
class TemplateModule : public Module, public T
{
protected:
    void init() override { return T::init(); }
    void ready() override { return T::ready(); }
    void deinit() override { return T::deinit(); }
};

class CustomModule : public Module
{
public:
    using StateF = std::function<void()>;

protected:
    UnorderedHashMap<State, StateF> f_;

protected:
    void init() override { if (auto f = f_.value(INIT, NULL)) f(); }
    void ready() override { if (auto f = f_.value(READY, NULL)) f(); }
    void deinit() override { if (auto f = f_.value(DEINIT, NULL)) f(); }
};

using ModuleFactory = Factory<Module*()>;
VE_API ModuleFactory& globalModuleFactory();
VE_API Dict<int>& globalModulePriority();

template<class C>
inline std::enable_if_t<std::is_base_of_v<Module, C>> registerModule(
    const std::string& key, int priority = 100, int ver = 0)
{
    globalModuleFactory().insertOne(key, [=] () -> Module* {
        return new C(key);
    });
    if (priority != 100)
        globalModulePriority()[key] = priority;
    if (ver > 0)
        version::manager().insertOne(key, [=] () -> int { return ver; });
}

namespace module {

inline Module* instance(const std::string& key) { return globalModuleFactory().instance(key); }
template<typename T> inline std::enable_if_t<std::is_base_of_v<Module, T>, T*> instance(const std::string& key)
{ return static_cast<T*>(instance(key)); }

}

}

VE_API std::ostream& operator<< (std::ostream& os, ve::Module::State s);

#define VE_REGISTER_MODULE(Key, Class) \
    VE_AUTO_RUN(ve::registerModule<Class>(#Key);)

#define VE_REGISTER_PRIORITY_MODULE(Key, Class, Priority, Ver) \
    VE_AUTO_RUN(ve::registerModule<Class>(#Key, Priority, Ver);)
