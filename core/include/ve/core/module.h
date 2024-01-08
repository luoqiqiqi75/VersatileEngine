// ----------------------------------------------------------------------------
// module.h
// ----------------------------------------------------------------------------
// This file is part of Versatile Engine
// ----------------------------------------------------------------------------
// Copyright (c) 2023 - 2023 Thilo, LuoQi, Qi Lu.
// Copyright (c) 2023 - 2023 Versatile Engine contributors (cf. AUTHORS.md)
//
// This file may be used under the terms of the GNU General Public License
// version 3.0 as published by the Free Software Foundation and appearing in
// the file LICENSE included in the packaging of this file.  Please review the
// following information to ensure the GNU General Public License version 3.0
// requirements will be met: http://www.gnu.org/copyleft/gpl.html.
//
// If you do not wish to use this file under the terms of the GPL version 3.0
// then you may purchase a commercial license. For more information contact
// <luoqiqiqi75@sina.com>.
//
// This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
// WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
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

class Module : public Object
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
    Module();
    ~Module();

    State state() const;

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
    HashMap<State, StateF> _f;

protected:
    void init() override { if (auto f = _f.value(INIT, NULL)) f(); }
    void ready() override { if (auto f = _f.value(READY, NULL)) f(); }
    void deinit() override { if (auto f = _f.value(DEINIT, NULL)) f(); }
};

using ModuleFactory = Factory<Module*()>;
ModuleFactory& globalModuleFactory();

template<class C>
inline basic::enable_if_void<std::is_base_of<Module, C>::value> registerModule(const std::string& key)
{
    globalModuleFactory().insertOne(key, [=] () -> Module* {
        data::at("_p.global_module_key")->set(nullptr, QString::fromStdString(key));
        return new C();
    });
}

template<class C>
inline void registerTemplateModule(const std::string& key)
{
    globalModuleFactory().insertOne(key, [=] () -> Module* {
        data::at("_p.global_module_key")->set(nullptr, QString::fromStdString(key));
        return new TemplateModule<C>();
    });
}

}

std::ostream& operator<< (std::ostream& os, ve::Module::State s);

#define VE_REGISTER_MODULE(Key, Class) VE_AUTO_RUN(ve::registerModule<Class>(#Key);)
