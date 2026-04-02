// ----------------------------------------------------------------------------
// log.cpp — ve::logDispatch implementation (pure C++17 + spdlog, no Qt)
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------

#include "ve/core/log.h"

#include <ctime>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <filesystem>

#ifdef _WIN32
#include <shlobj.h>   // SHGetKnownFolderPath
#pragma comment(lib, "shell32.lib")
#else
#include <cstdlib>    // getenv
#include <unistd.h>
#include <pwd.h>
#endif

#ifdef min
#undef min
#endif

#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"

namespace {

namespace fs = std::filesystem;

constexpr const char* ve_console_log_name    = "FConsole";
constexpr const char* ve_console_log_pattern = "%H:%M:%S.%e %^[%L] %v%$";

constexpr const char* ve_file_log_name    = "FFile";
constexpr const char* ve_file_log_pattern = "%L[%Y/%m/%d %H:%M:%S.%e] %v";

constexpr int ve_log_flush_dt = 3;

// --- App name / directory overrides ---
static std::string ve_app_name = "VersatileEngine";
static std::string ve_log_dir_override;     // set by ve::log::setLogDir()
static std::string ve_current_log_path;     // filled when file logger is created

// --- Minimum active level cache (atomic, cheap to read) ---
static std::atomic<int> ve_min_active_level{static_cast<int>(ve::LogLevel::Debug)};

// --- LogLevel <-> spdlog level mapping ---

spdlog::level::level_enum ve_to_spdlog(ve::LogLevel l)
{
    switch (l) {
        case ve::LogLevel::Debug:  return spdlog::level::debug;
        case ve::LogLevel::Info:   return spdlog::level::info;
        case ve::LogLevel::Waring: return spdlog::level::warn;
        case ve::LogLevel::Error:  return spdlog::level::err;
        case ve::LogLevel::Sudo:   return spdlog::level::critical;
        case ve::LogLevel::Ignore: return spdlog::level::off;
        default: return spdlog::level::trace;
    }
}

int ve_from_spdlog(spdlog::level::level_enum sl)
{
    switch (sl) {
        case spdlog::level::trace:
        case spdlog::level::debug:    return static_cast<int>(ve::LogLevel::Debug);
        case spdlog::level::info:     return static_cast<int>(ve::LogLevel::Info);
        case spdlog::level::warn:     return static_cast<int>(ve::LogLevel::Waring);
        case spdlog::level::err:      return static_cast<int>(ve::LogLevel::Error);
        case spdlog::level::critical: return static_cast<int>(ve::LogLevel::Sudo);
        case spdlog::level::off:      return static_cast<int>(ve::LogLevel::Sudo) + 1;
        default: return 0;
    }
}

// --- Loggers ---

std::shared_ptr<spdlog::logger> ve_global_console_logger;
std::shared_ptr<spdlog::logger> ve_global_file_logger;
bool ve_file_logger_inited = false;

void ve_update_min_level()
{
    int cl = ve_global_console_logger
             ? ve_from_spdlog(ve_global_console_logger->level())
             : static_cast<int>(ve::LogLevel::Sudo) + 1;
    int fl = ve_global_file_logger
             ? ve_from_spdlog(ve_global_file_logger->level())
             : static_cast<int>(ve::LogLevel::Sudo) + 1;
    ve_min_active_level.store(std::min(cl, fl), std::memory_order_relaxed);
}

// --- Platform-specific: get writable AppData / log directory ---
std::string ve_get_log_dir()
{
    if (!ve_log_dir_override.empty()) return ve_log_dir_override;

    // Try working directory first: ./log/
    std::string cwd_log = "log";
    std::error_code ec;
    fs::create_directories(cwd_log, ec);
    if (!ec) return cwd_log;

    // Fallback to platform-specific directory
#ifdef _WIN32
    // %LOCALAPPDATA%/<AppName>/log/
    PWSTR wpath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &wpath))) {
        std::wstring ws(wpath);
        CoTaskMemFree(wpath);
        fs::path p = fs::path(ws) / ve_app_name / "log";
        return p.string();
    }
    return "log";
#else
    // $XDG_DATA_HOME or ~/.local/share
    const char* xdg = std::getenv("XDG_DATA_HOME");
    fs::path base;
    if (xdg && *xdg) {
        base = fs::path(xdg);
    } else {
        const char* home = std::getenv("HOME");
        if (!home) {
            auto* pw = getpwuid(getuid());
            home = pw ? pw->pw_dir : "/tmp";
        }
        base = fs::path(home) / ".local" / "share";
    }
    return (base / ve_app_name / "log").string();
#endif
}

std::string ve_get_log_path()
{
    std::string log_dir = ve_get_log_dir();
    std::error_code ec;
    fs::create_directories(log_dir, ec);
    if (ec) {
        std::cerr << "[ve::log] Failed to create log directory: " << log_dir
                  << " (" << ec.message() << ")" << std::endl;
        return "";
    }

    auto t  = std::time(nullptr);
    auto lt = std::localtime(&t);
    std::ostringstream ss;
    ss << log_dir;
    if (!log_dir.empty() && log_dir.back() != '/' && log_dir.back() != '\\')
        ss << '/';
    ss << std::put_time(lt, "%Y-%m-%d_%H-%M-%S") << ".txt";
    return ss.str();
}

auto ve_create_console_logger()
{
    auto cl = spdlog::stdout_color_mt(ve_console_log_name);
    cl->set_pattern(ve_console_log_pattern);
    cl->set_level(spdlog::level::trace);
    return cl;
}

auto ve_create_file_logger()
{
    auto path = ve_get_log_path();
    if (path.empty()) return std::shared_ptr<spdlog::logger>{};
    ve_current_log_path = path;
    auto fl = spdlog::basic_logger_mt(ve_file_log_name, path);
    fl->set_pattern(ve_file_log_pattern);
    fl->set_level(spdlog::level::info);
    return fl;
}

void ve_ensure_file_logger()
{
    if (ve_file_logger_inited) return;
    ve_global_file_logger = spdlog::get(ve_file_log_name);
    if (!ve_global_file_logger) ve_global_file_logger = ve_create_file_logger();
    if (ve_global_file_logger) spdlog::flush_every(std::chrono::seconds(ve_log_flush_dt));
    ve_file_logger_inited = true;
}

} // anonymous namespace

// --- Init ---

bool ve_global_logger_inited = false;

void ve_init_logger()
{
    if (ve_global_logger_inited) return;
    ve_global_logger_inited = true;

    ve_global_console_logger = spdlog::get(ve_console_log_name);
    if (!ve_global_console_logger) ve_global_console_logger = ve_create_console_logger();

    ve_ensure_file_logger();
    ve_update_min_level();
}

VE_AUTO_RUN(ve_init_logger())

// --- logMinActiveLevel ---

VE_API int ve::logMinActiveLevel()
{
    return ve_min_active_level.load(std::memory_order_relaxed);
}

// --- logOnSink explicit instantiations ---

template<> VE_API void ve::logOnSink<ve::LogSink::Console, ve::LogLevel::Debug>(const std::string_view& sv)  { ve_global_console_logger->debug(sv); }
template<> VE_API void ve::logOnSink<ve::LogSink::File,    ve::LogLevel::Debug>(const std::string_view& sv)  { ve_ensure_file_logger(); if (ve_global_file_logger) ve_global_file_logger->debug(sv); }
template<> VE_API void ve::logOnSink<ve::LogSink::Console, ve::LogLevel::Info>(const std::string_view& sv)   { ve_global_console_logger->info(sv); }
template<> VE_API void ve::logOnSink<ve::LogSink::File,    ve::LogLevel::Info>(const std::string_view& sv)   { ve_ensure_file_logger(); if (ve_global_file_logger) ve_global_file_logger->info(sv); }
template<> VE_API void ve::logOnSink<ve::LogSink::Console, ve::LogLevel::Waring>(const std::string_view& sv) { ve_global_console_logger->warn(sv); }
template<> VE_API void ve::logOnSink<ve::LogSink::File,    ve::LogLevel::Waring>(const std::string_view& sv) { ve_ensure_file_logger(); if (ve_global_file_logger) ve_global_file_logger->warn(sv); }
template<> VE_API void ve::logOnSink<ve::LogSink::Console, ve::LogLevel::Error>(const std::string_view& sv)  { ve_global_console_logger->error(sv); }
template<> VE_API void ve::logOnSink<ve::LogSink::File,    ve::LogLevel::Error>(const std::string_view& sv)  { ve_ensure_file_logger(); if (ve_global_file_logger) ve_global_file_logger->error(sv); }
template<> VE_API void ve::logOnSink<ve::LogSink::Console, ve::LogLevel::Sudo>(const std::string_view& sv)   { ve_global_console_logger->critical(sv); }
template<> VE_API void ve::logOnSink<ve::LogSink::File,    ve::LogLevel::Sudo>(const std::string_view& sv)   { ve_ensure_file_logger(); if (ve_global_file_logger) ve_global_file_logger->critical(sv); }

// --- ve::log namespace ---

namespace ve::log {

// --- globalLoggerName ---

template<> VE_API const char* globalLoggerName<ve::LogSink::Console>() { return ve_console_log_name; }
template<> VE_API const char* globalLoggerName<ve::LogSink::File>()    { return ve_file_log_name; }

// --- enable / disable ---

template<> VE_API void enable<ve::LogSink::Console>()  { ve_global_console_logger->set_level(spdlog::level::trace); ve_update_min_level(); }
template<> VE_API void enable<ve::LogSink::File>()     { ve_ensure_file_logger(); if (ve_global_file_logger) { ve_global_file_logger->set_level(spdlog::level::trace); ve_update_min_level(); } }
template<> VE_API void disable<ve::LogSink::Console>() { ve_global_console_logger->set_level(spdlog::level::off); ve_update_min_level(); }
template<> VE_API void disable<ve::LogSink::File>()    { if (ve_global_file_logger) { ve_global_file_logger->set_level(spdlog::level::off); ve_update_min_level(); } }

// --- configure (call before first log) ---

VE_API void setAppName(const std::string& name) { ve_app_name = name; }
VE_API void setLogDir(const std::string& dir)   { ve_log_dir_override = dir; }

// --- setLevel ---

VE_API void setLevel(LogLevel level)
{
    auto sl = ve_to_spdlog(level);
    if (ve_global_console_logger) ve_global_console_logger->set_level(sl);
    ve_ensure_file_logger();
    if (ve_global_file_logger) ve_global_file_logger->set_level(sl);
    ve_update_min_level();
}

template<> VE_API void setLevel<ve::LogSink::Console>(LogLevel level)
{
    if (ve_global_console_logger) ve_global_console_logger->set_level(ve_to_spdlog(level));
    ve_update_min_level();
}

template<> VE_API void setLevel<ve::LogSink::File>(LogLevel level)
{
    ve_ensure_file_logger();
    if (ve_global_file_logger) ve_global_file_logger->set_level(ve_to_spdlog(level));
    ve_update_min_level();
}

// --- setPattern ---

template<> VE_API void setPattern<ve::LogSink::Console>(const std::string& pattern)
{
    if (ve_global_console_logger) ve_global_console_logger->set_pattern(pattern);
}

template<> VE_API void setPattern<ve::LogSink::File>(const std::string& pattern)
{
    ve_ensure_file_logger();
    if (ve_global_file_logger) ve_global_file_logger->set_pattern(pattern);
}

// --- flush ---

VE_API void setFlushInterval(int seconds)
{
    spdlog::flush_every(std::chrono::seconds(seconds));
}

VE_API void flush()
{
    if (ve_global_console_logger) ve_global_console_logger->flush();
    if (ve_global_file_logger) ve_global_file_logger->flush();
}

// --- query ---

VE_API std::string getLogFilePath()
{
    ve_ensure_file_logger();
    return ve_current_log_path;
}

}
