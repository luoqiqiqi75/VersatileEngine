// ----------------------------------------------------------------------------
// result.h — ve::Result: unified return type for Step / Pipeline / Command
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------

#pragma once

#include "var.h"

namespace ve {

class VE_API Result
{
public:
    enum Code : int {
        SUCCESS =  0,
        FAIL    = -1,
        ACCEPT  =  1,
    };

    Result() : _code(SUCCESS) {}
    Result(Code code) : _code(code) {}
    Result(int code, const Var& content) : _code(code), _content(content) {}
    Result(int code, const std::string& err) : _code(code), _content(err) {}
    Result(bool ok, int err = FAIL) : _code(ok ? SUCCESS : err) {}

    int        code() const { return _code; }
    const Var& content() const { return _content; }

    bool isSuccess()  const { return _code == 0; }
    bool isError()    const { return _code < 0; }
    bool isAccepted() const { return _code > 0; }

    Result& setContent(const Var& v) { _content = v; return *this; }
    Result& setContent(Var&& v)      { _content = std::move(v); return *this; }

    std::string toString() const;

    explicit operator bool() const { return isSuccess(); }

private:
    int _code;
    Var _content;
};

} // namespace ve
