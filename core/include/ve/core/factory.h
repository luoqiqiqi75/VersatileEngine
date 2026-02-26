// ----------------------------------------------------------------------------
// factory.h
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

namespace ve {

template<class Signature>
class Factory : public Object, public Dict<std::function<Signature>>
{
public:
    typedef std::function<Signature> FunctionT;
    typedef basic::FInfo<FunctionT> FInfoT;
    typedef typename FInfoT::RetT RetT;

public:
    explicit Factory(const std::string& name) : Object(name) {}
    virtual ~Factory() {}

    template<typename... Params>
    RetT exec(const std::string& key, Params&&... params) { return this->has(key) ? (*this)[key](std::forward<Params>(params)...) : (RetT)(NULL); }
    RetT exec(const std::string& key) { return this->has(key) ? (*this)[key]() : (RetT)(NULL); }

    template<typename T, typename... Params>
    T execAs(const std::string& key, Params&&... params) { return static_cast<T>(exec(key, std::forward<Params>(params)...)); }
};

template<typename T, typename... Ts>
T& instance(Ts&&... ts) { static T t(std::forward<Ts>(ts)...); return t; }

}
