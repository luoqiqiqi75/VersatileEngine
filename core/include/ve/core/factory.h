// ----------------------------------------------------------------------------
// factory.h
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
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

}
