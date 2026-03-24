// ----------------------------------------------------------------------------
// module.h - Module lifecycle
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------
//
// Lifecycle (driven by ve::entry):
//   parents first:  all constructors → all init()
//   children first: all ready() → ... → all deinit() → all destructors
//
// Module key "a.b" is a child of "a". Priority inheritance ensures
// a child never precedes its nearest registered ancestor.
//
// Convention:
//   constructor:  read node() config, create own resources (no cross-module deps)
//   init():       data tree setup, register commands (parent before child)
//   ready():      start services, subscribe to data (child before parent)
//   deinit():     stop services, disconnect (child before parent)
//   destructor:   release own resources (child before parent)
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

    /// Module workspace node (key with '.' -> '/', e.g. "ve.core" -> "ve/core")
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

#define VE_REGISTER_MODULE(Key, Class, ...) \
    namespace { int PRIVATE_VE_AUTO_RUN_FUNC() { \
        ve::registerModule<Class>(#Key, 100, ## __VA_ARGS__); \
        return 0; \
    } int PRIVATE_VE_AUTO_RUN_VAR = PRIVATE_VE_AUTO_RUN_FUNC(); /* NOLINT */ }

#define VE_REGISTER_PRIORITY_MODULE(Key, Class, ...) \
    namespace { int PRIVATE_VE_AUTO_RUN_FUNC() { \
        ve::registerModule<Class>(#Key, __VA_ARGS__); \
        return 0; \
    } int PRIVATE_VE_AUTO_RUN_VAR = PRIVATE_VE_AUTO_RUN_FUNC(); /* NOLINT */ }
