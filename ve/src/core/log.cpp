// ----------------------------------------------------------------------------
// log.cpp - spdlog configuration and dispatch
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------

#include "ve/core/log.h"
#include "ve/core/node.h"

#include <ctime>
#include <iomanip>
#include <filesystem>
#include <limits>
#include <cctype>
#include <cstdlib>

#ifdef _WIN32
#include <shlobj.h>
#pragma comment(lib, "shell32.lib")
#else
#include <unistd.h>
#include <pwd.h>
#endif

#ifdef min
#undef min
#endif

#include "spdlog/spdlog.h"
#include "spdlog/async.h"
#include "spdlog/details/periodic_worker.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"

namespace {

namespace fs = std::filesystem;
struct LogState
{
    std::shared_ptr<spdlog::details::thread_pool> pool;
    std::shared_ptr<spdlog::logger> console, file, combined;
    std::unique_ptr<spdlog::details::periodic_worker> flusher;

    ~LogState()
    {
        // Stop enqueueing, drain/join the pool, then flush the sinks directly.
        flusher.reset();
        pool.reset();
        if (combined) {
            for (auto& sink : combined->sinks()) {
                try { sink->flush(); }
                catch (const std::exception& e) { std::cerr << "[ve/log] " << e.what() << '\n'; }
            }
        }
    }
};
LogState g;

static constexpr spdlog::level::level_enum _level(ve::LogLevel l)
{
    switch (l) {
        case ve::LogLevel::Debug: return spdlog::level::debug;
        case ve::LogLevel::Info: return spdlog::level::info;
        case ve::LogLevel::Waring: return spdlog::level::warn;
        case ve::LogLevel::Error: return spdlog::level::err;
        case ve::LogLevel::Sudo: return spdlog::level::critical;
        default: return spdlog::level::off;
    }
}

static std::string _log_dir(ve::Node* n)
{
    auto dir = n->get("log/dir").toString();
    if (!dir.empty()) return dir;
    std::error_code ec;
    fs::create_directories("log", ec);
    if (!ec) return "log";
    auto app = n->get("app").toString("VersatileEngine");
#ifdef _WIN32
    PWSTR wpath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &wpath))) {
        fs::path base(wpath);
        CoTaskMemFree(wpath);
        return (base / app / "log").string();
    }
    return "log";
#else
    const char* xdg = std::getenv("XDG_DATA_HOME");
    fs::path base;
    if (xdg && *xdg) {
        base = xdg;
    } else {
        const char* home = std::getenv("HOME");
        if (!home) {
            auto* pw = getpwuid(getuid());
            home = pw ? pw->pw_dir : "/tmp";
        }
        base = fs::path(home) / ".local" / "share";
    }
    return (base / app / "log").string();
#endif
}

static std::string _log_path(ve::Node* n)
{
    fs::path dir(_log_dir(n));
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        std::cerr << "[ve/log] Cannot create log directory: " << dir << " (" << ec.message() << ")\n";
        return {};
    }
    auto t = std::time(nullptr);
    std::tm lt = *std::localtime(&t);
    std::ostringstream name;
    name << std::put_time(&lt, "%Y-%m-%d_%H-%M-%S") << ".txt";
    return (dir / name.str()).string();
}

static bool _log_level(const ve::Var& v, spdlog::level::level_enum& l)
{
    if (v.isNull()) return true;
    auto s = v.toString();
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    if (s == "d" || s == "debug" || s == "trace") l = spdlog::level::debug;
    else if (s == "i" || s == "info" || s.empty()) l = spdlog::level::info;
    else if (s == "w" || s == "warn" || s == "warning" || s == "waring") l = spdlog::level::warn;
    else if (s == "e" || s == "err" || s == "error") l = spdlog::level::err;
    else if (s == "s" || s == "sudo" || s == "critical") l = spdlog::level::critical;
    else if (s == "off" || s == "ignore") l = spdlog::level::off;
    else { std::cerr << "[ve/log] Unknown log level: " << s << '\n'; return false; }
    return true;
}

template<bool Console = true, bool File = true> static spdlog::logger* _logger()
{
    if (!g.combined) {
        ve::Node defaults;
        ve::log::configure(&defaults);
    }
    if constexpr (Console && File) return g.combined.get();
    else if constexpr (Console) return g.console.get();
    else return g.file.get();
}

} // namespace

template<bool Console, bool File> bool ve::internal::logEnabled(LogLevel level)
{
    auto* logger = _logger<Console, File>();
    return logger && logger->should_log(_level(level));
}

template VE_API bool ve::internal::logEnabled<true, true>(LogLevel);
template VE_API bool ve::internal::logEnabled<true, false>(LogLevel);
template VE_API bool ve::internal::logEnabled<false, true>(LogLevel);

template<ve::LogLevel L, bool Console, bool File>
void ve::internal::logWrite(const std::string_view& sv)
{
    auto* logger = _logger<Console, File>();
    if (logger) logger->log(_level(L), spdlog::string_view_t(sv.data(), sv.size()));
}

template VE_API void ve::internal::logWrite<ve::LogLevel::Debug, true, true>(const std::string_view&);
template VE_API void ve::internal::logWrite<ve::LogLevel::Debug, true, false>(const std::string_view&);
template VE_API void ve::internal::logWrite<ve::LogLevel::Debug, false, true>(const std::string_view&);
template VE_API void ve::internal::logWrite<ve::LogLevel::Info, true, true>(const std::string_view&);
template VE_API void ve::internal::logWrite<ve::LogLevel::Info, true, false>(const std::string_view&);
template VE_API void ve::internal::logWrite<ve::LogLevel::Info, false, true>(const std::string_view&);
template VE_API void ve::internal::logWrite<ve::LogLevel::Waring, true, true>(const std::string_view&);
template VE_API void ve::internal::logWrite<ve::LogLevel::Waring, true, false>(const std::string_view&);
template VE_API void ve::internal::logWrite<ve::LogLevel::Waring, false, true>(const std::string_view&);
template VE_API void ve::internal::logWrite<ve::LogLevel::Error, true, true>(const std::string_view&);
template VE_API void ve::internal::logWrite<ve::LogLevel::Error, true, false>(const std::string_view&);
template VE_API void ve::internal::logWrite<ve::LogLevel::Error, false, true>(const std::string_view&);
template VE_API void ve::internal::logWrite<ve::LogLevel::Sudo, true, true>(const std::string_view&);
template VE_API void ve::internal::logWrite<ve::LogLevel::Sudo, true, false>(const std::string_view&);
template VE_API void ve::internal::logWrite<ve::LogLevel::Sudo, false, true>(const std::string_view&);

namespace ve::log {

VE_API bool configure(Node* n)
{
    if (!n) return false;
    auto level = spdlog::level::info;
    auto flush = spdlog::level::err;
    if (!_log_level(n->get("log/level"), level)) return false;
    auto cl = level, fl = level;
    if (!_log_level(n->get("log/console/level"), cl) ||
        !_log_level(n->get("log/file/level"), fl) ||
        !_log_level(n->get("log/flush_level"), flush)) return false;
    bool ce = n->get("log/console/enabled").toBool(true);
    bool fe = n->get("log/file/enabled").toBool(true);
    auto queue = n->get("log/queue_size").toInt64(8192);
    auto workers = n->get("log/worker_threads").toInt64(1);
    auto interval = n->get("log/flush_interval_seconds").toInt64(3);
    if (queue <= 0 || (uint64_t)queue >= std::numeric_limits<size_t>::max()) {
        std::cerr << "[ve/log] log/queue_size is out of range\n";
        return false;
    }
    if (workers <= 0 || workers > 1000) {
        std::cerr << "[ve/log] log/worker_threads must be in 1..1000\n";
        return false;
    }
    if (interval < 0 || interval > std::numeric_limits<int>::max()) {
        std::cerr << "[ve/log] log/flush_interval_seconds is out of range\n";
        return false;
    }
    auto policy = n->get("log/overflow_policy").toString("block");
    if (policy != "block" && policy != "overrun_oldest") {
        std::cerr << "[ve/log] log/overflow_policy must be block or overrun_oldest\n";
        return false;
    }
    try {
        LogState next;
        Vector<spdlog::sink_ptr> sinks;
        if (n->get("log/async").toBool(false) && (ce || fe))
            next.pool = std::make_shared<spdlog::details::thread_pool>((size_t)queue, (size_t)workers);
        auto make = [&](const char* name, const Vector<spdlog::sink_ptr>& ss, spdlog::level::level_enum l) {
            std::shared_ptr<spdlog::logger> r;
            if (next.pool) r = std::make_shared<spdlog::async_logger>(name, ss.begin(), ss.end(), next.pool,
                policy == "block" ? spdlog::async_overflow_policy::block : spdlog::async_overflow_policy::overrun_oldest);
            else r = std::make_shared<spdlog::logger>(name, ss.begin(), ss.end());
            r->set_level(l);
            r->flush_on(flush);
            return r;
        };
        if (ce) {
            auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            sink->set_pattern(n->get("log/console/pattern").toString("%H:%M:%S.%e %^[%L] %v%$"));
            sink->set_level(cl);
            sinks.push_back(sink);
            next.console = make("FConsole", {sink}, cl);
        }
        if (fe) {
            auto path = _log_path(n);
            if (path.empty()) return false;
            auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path);
            sink->set_pattern(n->get("log/file/pattern").toString("%L[%Y/%m/%d %H:%M:%S.%e] %v"));
            sink->set_level(fl);
            sinks.push_back(sink);
            next.file = make("FFile", {sink}, fl);
        }
        next.combined = make("ve", sinks, std::min(ce ? cl : spdlog::level::off, fe ? fl : spdlog::level::off));
        if (interval > 0 && !sinks.empty()) next.flusher = std::make_unique<spdlog::details::periodic_worker>(
            [logger = next.combined] { logger->flush(); }, std::chrono::seconds(interval));
        g.pool.swap(next.pool);
        g.console.swap(next.console);
        g.file.swap(next.file);
        g.combined.swap(next.combined);
        g.flusher.swap(next.flusher);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ve/log] " << e.what() << '\n';
        return false;
    }
}

VE_API void flush() { if (auto* logger = _logger()) logger->flush(); }

VE_API std::string getLogFilePath()
{
    _logger();
    return g.file ? std::static_pointer_cast<spdlog::sinks::basic_file_sink_mt>(g.file->sinks().front())->filename() : std::string{};
}

} // namespace ve::log
