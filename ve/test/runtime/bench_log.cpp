// Compare producer cost with complete drain time; console I/O is excluded.
#include "ve/core/log.h"
#include "ve/entry.h"
#include <fstream>
#include <filesystem>
#include <iomanip>

int main()
{
    namespace fs = std::filesystem;
    using Clock = std::chrono::steady_clock;
    const auto dir = fs::temp_directory_path() / ("ve_log_bench_" + std::to_string(Clock::now().time_since_epoch().count()));
    const std::string payload(256, 'x');
    ve::Node disabled;
    disabled.set("log/console/enabled", false);
    disabled.set("log/file/enabled", false);
    constexpr int per_thread = 50000;
    std::cout << "mode,producers,messages,producer_ms,total_ms,dropped\n";
    for (unsigned producers : {1u, 4u}) {
        for (int mode = 0; mode < 4; ++mode) {
            ve::node::root()->erase("ve/entry");
            ve::Node options;
            options.at("config_file")->set("__ve_bench_missing.json");
            options.at("blacklist")->append("")->set("ve");
            options.at("log/console/enabled")->set(false);
            options.at("log/level")->set(mode == 0 ? "off" : "info");
            options.at("log/dir")->set((dir / (std::to_string(producers) + "_" + std::to_string(mode))).string());
            options.at("log/async")->set(mode >= 2);
            options.at("log/flush_interval_seconds")->set(0);
            options.at("log/flush_level")->set("off");
            options.at("log/overflow_policy")->set(mode == 3 ? "overrun_oldest" : "block");
            if (!ve::entry::setup(&options)) return 1;
            ve::entry::init();
            auto log_path = ve::log::getLogFilePath();
            std::vector<std::thread> threads;
            auto begin = Clock::now();
            for (unsigned p = 0; p < producers; ++p) {
                threads.emplace_back([&] {
                    for (int i = 0; i < per_thread; ++i) { veLogI << i << ' ' << payload; }
                });
            }
            for (auto& thread : threads) { thread.join(); }
            auto enqueued = Clock::now();
            ve::entry::deinit();
            if (!ve::log::configure(&disabled)) return 1;
            auto drained = Clock::now();
            size_t written = 0;
            { std::ifstream log_file(log_path); for (std::string line; std::getline(log_file, line);) { ++written; } }
            auto dropped = mode == 0 ? size_t(0) : producers * per_thread - written;
            std::cout << (mode == 0 ? "filtered" : mode == 1 ? "sync" : mode == 2 ? "async_block" : "async_overrun_oldest")
                      << ',' << producers << ',' << producers * per_thread << ',' << std::fixed << std::setprecision(2)
                      << std::chrono::duration<double, std::milli>(enqueued - begin).count() << ','
                      << std::chrono::duration<double, std::milli>(drained - begin).count() << ',' << dropped << '\n';
        }
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}
