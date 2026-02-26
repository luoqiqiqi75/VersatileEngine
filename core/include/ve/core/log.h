// ----------------------------------------------------------------------------
// log.h
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

#include "ve/global.h"

#include <iostream>
#include <sstream>
#include <string_view>

#ifndef VE_LOG_DISABLE_CONSOLE
#define VE_LOG_ENABLE_CONSOLE
#endif
#ifndef VE_LOG_DISABLE_FILE
#define VE_LOG_ENABLE_FILE
#endif

namespace ve {

enum class LogSink
{
    Console,
    File
};

enum class LogLevel
{
    Ignore  = -1,
    Debug   = 0,
    Info,
    Waring,
    Error,
    Sudo
};

template<LogSink S, LogLevel L> VE_API void LogOnSink(const std::string_view& sv);

template<LogLevel L> inline void Log(const std::string_view& sv)
{
#ifdef VE_LOG_ENABLE_CONSOLE
    LogOnSink<LogSink::Console, L>(sv);
#endif
#ifdef VE_LOG_ENABLE_FILE
    LogOnSink<LogSink::File, L>(sv);
#endif
}

template<typename T> inline std::ostream& OStream(std::ostream& os, T&& t) { os << std::forward<T>(t); return os; }

template<LogLevel L, bool Spacing> struct LogStream;

template<typename DerivedT> struct NoLogStreamBase
{
    template<typename... Ts> DerivedT& operator<< (Ts...) { return *static_cast<DerivedT*>(this); }
    template<typename... Ts> void operator() (Ts...) const {}
};

template<> struct LogStream<LogLevel::Ignore, true> : NoLogStreamBase<LogStream<LogLevel::Ignore, true>>{};
template<> struct LogStream<LogLevel::Ignore, false> : NoLogStreamBase<LogStream<LogLevel::Ignore, false>>{};

template<LogLevel L> struct LogStream<L, false>
{
    std::ostringstream oss;

    template<typename T> inline LogStream& operator<< (T&& t) { OStream(oss, std::forward<T>(t)); return *this; }
    LogStream& operator<< (std::ostream& (*f)(std::ostream&)) { f(oss); return *this; }

    template<typename T> void operator() (T&& t) { OStream(oss, std::forward<T>(t)); }
    template<typename T, typename... Ts> void operator() (T t, Ts&&... ts) { OStream(oss, std::forward<T>(t)); this->operator()(std::forward<Ts>(ts)...); }

    ~LogStream() { Log<L>(oss.str()); }
};

template<LogLevel L> struct LogStream<L, true>
{
    std::ostringstream oss;

    template<typename T> LogStream& operator<< (T&& t) { OStream(oss, std::forward<T>(t)) << ' '; return *this; }
    LogStream& operator<< (std::ostream& (*f)(std::ostream&)) { f(oss); return *this; }

    template<typename T> void operator() (T&& t) { OStream(oss, std::forward<T>(t)); }
    template<typename T, typename... Ts> void operator() (T t, Ts&&... ts) { OStream(oss, std::forward<T>(t)) << ' '; this->operator()(std::forward<Ts>(ts)...); }

    ~LogStream()
    {
        auto str = oss.str();
        if (!str.empty()) Log<L>(std::string_view(str.c_str(), str.find_last_not_of(' ') + 1));
    }
};

}

#ifdef VE_LOG_ENABLE_DETAIL
#define veLogD (ve::LogStream<ve::LogLevel::Debug, false>() << __FUNCTION__ << ' ')
#define veLogDs (ve::LogStream<ve::LogLevel::Debug, true>() << __FUNCTION__ << ' ')
#else
#define veLogD ve::LogStream<ve::LogLevel::Debug, false>()
#define veLogDs ve::LogStream<ve::LogLevel::Debug, true>()
#endif

#define veLogI ve::LogStream<ve::LogLevel::Info, false>()
#define veLogW ve::LogStream<ve::LogLevel::Waring, false>()
#define veLogE ve::LogStream<ve::LogLevel::Error, false>()
#define veLogS ve::LogStream<ve::LogLevel::Sudo, false>()

#define veLogIs ve::LogStream<ve::LogLevel::Info, true>()
#define veLogWs ve::LogStream<ve::LogLevel::Waring, true>()
#define veLogEs ve::LogStream<ve::LogLevel::Error, true>()
#define veLogSs ve::LogStream<ve::LogLevel::Sudo, true>()

#define veLog veLogI

namespace ve::log {
template<typename... Ts> inline void I(Ts... ts) { veLogI(std::forward<Ts>(ts)...); };
template<typename... Ts> inline void Is(Ts... ts) { veLogIs(std::forward<Ts>(ts)...); };
template<typename... Ts> inline void W(Ts... ts) { veLogW(std::forward<Ts>(ts)...); };
template<typename... Ts> inline void Ws(Ts... ts) { veLogWs(std::forward<Ts>(ts)...); };
template<typename... Ts> inline void E(Ts... ts) { veLogE(std::forward<Ts>(ts)...); };
template<typename... Ts> inline void Es(Ts... ts) { veLogEs(std::forward<Ts>(ts)...); };

template<typename... Ts> inline void D(Ts... ts) { BLogD(std::forward<Ts>(ts)...); };
template<typename... Ts> inline void Ds(Ts... ts) { BLogDs(std::forward<Ts>(ts)...); };
namespace internal { template<int> inline auto gen_cnt() { static std::atomic_uint64_t cnt = 0; return cnt++; } }
template<int I = 0, typename... Ts> inline void Cnt(Ts... ts) { BLogDs(internal::gen_cnt<I>(), std::forward<Ts>(ts)...); };
template<int N = 40, char C = '-'> inline void Line() { static std::string str(N, C); veLogD(str); }
template<int N = 10> inline void Blank() { Line<N, '\n'>(); }

// advance usage
template<ve::LogSink S> VE_API const char* GlobalLoggerName();
template<ve::LogSink S> VE_API void Enable();
template<ve::LogSink S> VE_API void Disable();
}

