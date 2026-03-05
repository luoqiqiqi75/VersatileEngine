// ----------------------------------------------------------------------------
// log.h
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
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

template<LogSink S, LogLevel L> VE_API void logOnSink(const std::string_view& sv);

template<LogLevel L> inline void logDispatch(const std::string_view& sv)
{
#ifdef VE_LOG_ENABLE_CONSOLE
    logOnSink<LogSink::Console, L>(sv);
#endif
#ifdef VE_LOG_ENABLE_FILE
    logOnSink<LogSink::File, L>(sv);
#endif
}

template<typename T> inline std::ostream& streamPut(std::ostream& os, T&& t) { os << std::forward<T>(t); return os; }

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

    template<typename T> inline LogStream& operator<< (T&& t) { streamPut(oss, std::forward<T>(t)); return *this; }
    LogStream& operator<< (std::ostream& (*f)(std::ostream&)) { f(oss); return *this; }

    template<typename T> void operator() (T&& t) { streamPut(oss, std::forward<T>(t)); }
    template<typename T, typename... Ts> void operator() (T t, Ts&&... ts) { streamPut(oss, std::forward<T>(t)); this->operator()(std::forward<Ts>(ts)...); }

    ~LogStream() { logDispatch<L>(oss.str()); }
};

template<LogLevel L> struct LogStream<L, true>
{
    std::ostringstream oss;

    template<typename T> LogStream& operator<< (T&& t) { streamPut(oss, std::forward<T>(t)) << ' '; return *this; }
    LogStream& operator<< (std::ostream& (*f)(std::ostream&)) { f(oss); return *this; }

    template<typename T> void operator() (T&& t) { streamPut(oss, std::forward<T>(t)); }
    template<typename T, typename... Ts> void operator() (T t, Ts&&... ts) { streamPut(oss, std::forward<T>(t)) << ' '; this->operator()(std::forward<Ts>(ts)...); }

    ~LogStream()
    {
        auto str = oss.str();
        if (!str.empty()) logDispatch<L>(std::string_view(str.c_str(), str.find_last_not_of(' ') + 1));
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
template<typename... Ts> inline void i(Ts... ts) { veLogI(std::forward<Ts>(ts)...); };
template<typename... Ts> inline void is(Ts... ts) { veLogIs(std::forward<Ts>(ts)...); };
template<typename... Ts> inline void w(Ts... ts) { veLogW(std::forward<Ts>(ts)...); };
template<typename... Ts> inline void ws(Ts... ts) { veLogWs(std::forward<Ts>(ts)...); };
template<typename... Ts> inline void e(Ts... ts) { veLogE(std::forward<Ts>(ts)...); };
template<typename... Ts> inline void es(Ts... ts) { veLogEs(std::forward<Ts>(ts)...); };

template<typename... Ts> inline void d(Ts... ts) { veLogD(std::forward<Ts>(ts)...); };
template<typename... Ts> inline void ds(Ts... ts) { veLogDs(std::forward<Ts>(ts)...); };
namespace internal { template<int> inline auto genCnt() { static std::atomic_uint64_t cnt = 0; return cnt++; } }
template<int I = 0, typename... Ts> inline void cnt(Ts... ts) { veLogDs(internal::genCnt<I>(), std::forward<Ts>(ts)...); };
template<int N = 40, char C = '-'> inline void line() { static std::string str(N, C); veLogD(str); }
template<int N = 10> inline void blank() { line<N, '\n'>(); }

// configure — call before first log to take effect
VE_API void setAppName(const std::string& name);

// advance usage
template<ve::LogSink S> VE_API const char* globalLoggerName();
template<ve::LogSink S> VE_API void enable();
template<ve::LogSink S> VE_API void disable();
}
