#include "ve/core/log.h"

#include <time.h>
#include <iostream>
#include <iomanip>

#include <QDir>
#include <QStandardPaths>
#include <QCoreApplication>

#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/async.h"

namespace {

constexpr const char* ve_console_log_name = "FConsole";
constexpr const char* ve_console_log_pattern = "%H:%M:%S.%e %^[%L] %v%$"; // color only one section

constexpr const char* ve_file_log_name = "FFile";
constexpr const char* ve_file_log_pattern = "%L[%Y/%m/%d %H:%M:%S.%e] %v";

const int ve_log_flush_dt = 3;

std::string ve_get_log_path()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString log_dir = base.isEmpty() ? QString("log/") : (base + "/log/");
    if (!QDir().mkpath(log_dir)) {
        qCritical() << "Failed to create log file path" << log_dir;
        return "";
    }

    auto t = std::time(nullptr);
    auto lt = std::localtime(&t);
    std::stringstream ss;
    ss << log_dir.toStdString() << std::put_time(lt, "%F_%H-%M-%S") << ".txt";
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
    auto fl = spdlog::basic_logger_mt(ve_file_log_name, ve_get_log_path());
    fl->set_pattern(ve_file_log_pattern);
    fl->set_level(spdlog::level::info);
    return fl;
}

std::shared_ptr<spdlog::logger> ve_global_console_logger;
std::shared_ptr<spdlog::logger> ve_global_file_logger;
bool ve_file_logger_inited = false;

void ve_ensure_file_logger() {
    if (ve_file_logger_inited) return;
    auto path = ve_get_log_path();
    if (path.empty()) return;
    ve_global_file_logger = spdlog::get(ve_file_log_name);
    if (!ve_global_file_logger) ve_global_file_logger = ve_create_file_logger();
    spdlog::flush_every(std::chrono::seconds(ve_log_flush_dt));
    ve_file_logger_inited = true;
}

}

bool ve_global_logger_inited = false;

void ve_init_logger() {
    if (ve_global_logger_inited) return;
    ve_global_logger_inited = true;

    // Console logger can always be created immediately
    ve_global_console_logger = spdlog::get(ve_console_log_name);
    if (!ve_global_console_logger) ve_global_console_logger = ve_create_console_logger();

    // Try file logger now; if QApp not ready yet, it will be retried on first use
    ve_ensure_file_logger();
}

VE_AUTO_RUN(ve_init_logger())

template<> VE_API void ve::LogOnSink<ve::LogSink::Console, ve::LogLevel::Debug>(const std::string_view& sv) { ve_global_console_logger->debug(sv); }
template<> VE_API void ve::LogOnSink<ve::LogSink::File, ve::LogLevel::Debug>(const std::string_view& sv) { ve_ensure_file_logger(); if (ve_global_file_logger) ve_global_file_logger->debug(sv); }
template<> VE_API void ve::LogOnSink<ve::LogSink::Console, ve::LogLevel::Info>(const std::string_view& sv) { ve_global_console_logger->info(sv); }
template<> VE_API void ve::LogOnSink<ve::LogSink::File, ve::LogLevel::Info>(const std::string_view& sv) { ve_ensure_file_logger(); if (ve_global_file_logger) ve_global_file_logger->info(sv); }
template<> VE_API void ve::LogOnSink<ve::LogSink::Console, ve::LogLevel::Waring>(const std::string_view& sv) { ve_global_console_logger->warn(sv); }
template<> VE_API void ve::LogOnSink<ve::LogSink::File, ve::LogLevel::Waring>(const std::string_view& sv) { ve_ensure_file_logger(); if (ve_global_file_logger) ve_global_file_logger->warn(sv); }
template<> VE_API void ve::LogOnSink<ve::LogSink::Console, ve::LogLevel::Error>(const std::string_view& sv) { ve_global_console_logger->error(sv); }
template<> VE_API void ve::LogOnSink<ve::LogSink::File, ve::LogLevel::Error>(const std::string_view& sv) { ve_ensure_file_logger(); if (ve_global_file_logger) ve_global_file_logger->error(sv); }
template<> VE_API void ve::LogOnSink<ve::LogSink::Console, ve::LogLevel::Sudo>(const std::string_view& sv) { ve_global_console_logger->critical(sv); }
template<> VE_API void ve::LogOnSink<ve::LogSink::File, ve::LogLevel::Sudo>(const std::string_view& sv) { ve_ensure_file_logger(); if (ve_global_file_logger) ve_global_file_logger->critical(sv); }

namespace ve::log {

template<> VE_API const char* GlobalLoggerName<ve::LogSink::Console>() { return ve_console_log_name; }
template<> VE_API const char* GlobalLoggerName<ve::LogSink::File>() { return ve_file_log_name; }

template<> VE_API void Enable<ve::LogSink::Console>() { ve_global_console_logger->set_level(spdlog::level::trace); }
template<> VE_API void Enable<ve::LogSink::File>()
{
    ve_ensure_file_logger();
    if (ve_global_file_logger) ve_global_file_logger->set_level(spdlog::level::trace);
}
template<> VE_API void Disable<ve::LogSink::Console>() { ve_global_console_logger->set_level(spdlog::level::off); }
template<> VE_API void Disable<ve::LogSink::File>() { if (ve_global_file_logger) ve_global_file_logger->set_level(spdlog::level::off); }

}
