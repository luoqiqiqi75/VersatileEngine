// ----------------------------------------------------------------------------
// module.h
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------

#pragma once

#include "base.h"
#include "data.h"
#include "factory.h"

namespace ve {

enum ModuleSignal : int
{
    MODULE_STATE_ABOUT_TO_CHANGE    = 0xffe0,
    MODULE_STATE_CHANGED            = 0xffe1
};

class VE_API Module : public Object
{
public:
    enum State : int {
        NONE    = 0x0f00 | 0x00,
        INIT    = 0x0f00 | 0x10,
        READY   = 0x0f00 | 0x80,
        DEINIT  = 0x0f00 | 0xf0
    };

public:
    Module();
    ~Module();

public:
    State state() const;

    template<State s> void exeState();

protected:
    virtual void init();
    virtual void ready();
    virtual void deinit();

private:
    VE_DECLARE_PRIVATE
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
    HashMap<State, StateF> _f;

protected:
    void init() override { if (auto f = _f.value(INIT, NULL)) f(); }
    void ready() override { if (auto f = _f.value(READY, NULL)) f(); }
    void deinit() override { if (auto f = _f.value(DEINIT, NULL)) f(); }
};

using ModuleFactory = Factory<Module*()>;
VE_API ModuleFactory& globalModuleFactory();

template<class C>
inline basic::enable_if_void<std::is_base_of<Module, C>::value> registerModule(const std::string& key)
{
    globalModuleFactory().insertOne(key, [=] () -> Module* {
        data::create(nullptr, "ve.module.global_module_key")->set(nullptr, QString::fromStdString(key));
        return new C();
    });
}

template<class C>
inline void registerTemplateModule(const std::string& key)
{
    globalModuleFactory().insertOne(key, [=] () -> Module* {
        data::create(nullptr, "ve.module.global_module_key")->set(nullptr, QString::fromStdString(key));
        return new TemplateModule<C>();
    });
}

namespace module {

VE_API Module* instance(const std::string& key);
template<typename T> inline basic::enable_if_t<std::is_base_of_v<Module, T>, T*> instance(const std::string& key)
{ return static_cast<T*>(instance(key)); }

}

}

VE_API std::ostream& operator<< (std::ostream& os, ve::Module::State s);

#define VE_REGISTER_MODULE(Key, Class) VE_AUTO_RUN(ve::registerModule<Class>(#Key);)
