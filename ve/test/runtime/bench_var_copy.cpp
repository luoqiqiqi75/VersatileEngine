// Release benchmark for Var ownership operations, independent of Node locking.
// Each case has one warmup and seven measured runs. Threads copy one shared,
// immutable source; the source itself is never concurrently changed.
// Optional arguments select one operation and byte size for isolated runs.
#include "ve/core/var.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

namespace {
std::atomic<uint64_t> checksum{0};
std::string selected_operation;
int selected_bytes = -1;

template<class Fn>
void bench(const char* operation, int bytes, int workers, int iterations, Fn fn)
{
    if (!selected_operation.empty() && selected_operation != operation) return;
    if (selected_bytes >= 0 && selected_bytes != bytes) return;
    std::vector<double> samples;
    for (int repeat = -1; repeat < 7; ++repeat) {
        std::atomic<int> ready{0};
        std::atomic<bool> start{false};
        std::vector<std::thread> threads;
        for (int t = 0; t < workers; ++t) {
            threads.emplace_back([&] {
                uint64_t sum = 0;
                ++ready;
                while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
                for (int i = 0; i < iterations; ++i) sum += fn(i);
                checksum.fetch_add(sum, std::memory_order_relaxed);
            });
        }
        while (ready.load() != workers) std::this_thread::yield();
        const auto begin = std::chrono::steady_clock::now();
        start.store(true, std::memory_order_release);
        for (auto& thread : threads) thread.join();
        const double ns = std::chrono::duration<double, std::nano>(
            std::chrono::steady_clock::now() - begin).count();
        if (repeat >= 0) samples.push_back(ns / (iterations * workers));
    }
    std::sort(samples.begin(), samples.end());
    const double median = samples[samples.size() / 2];
    std::cout << operation << ',' << bytes << ',' << workers << ',' << std::fixed
              << std::setprecision(2) << median << ',' << 1000.0 / median << '\n';
}
}

int main(int argc, char** argv)
{
    if (argc > 1) selected_operation = argv[1];
    if (argc > 2) selected_bytes = std::stoi(argv[2]);
    std::cout << "operation,bytes,threads,median_ns_per_op,million_ops_per_second\n";
    const ve::Var number(42);
    bench("int.copy", 0, 1, 100000, [&](int) {
        ve::Var copy(number);
        return copy.type();
    });
    for (int bytes : {8, 256, 4096, 65536}) {
        const int iterations = bytes >= 65536 ? 10000 : 100000;
        const std::string text(bytes, 'x');
        const ve::Var string(text);
        const ve::Var binary(ve::Bytes(bytes, uint8_t(42)));
        for (int workers : {1, 4, 8}) {
            bench("string.copy", bytes, workers, iterations, [&](int) {
                ve::Var copy(string);
                return copy.type();
            });
            bench("bin.copy", bytes, workers, iterations, [&](int) {
                ve::Var copy(binary);
                return copy.type();
            });
        }
        bench("string.copy.toString", bytes, 1, iterations, [&](int) {
            ve::Var copy(string);
            return copy.toString().size();
        });
        bench("bin.copy.toBin", bytes, 1, iterations, [&](int) {
            ve::Var copy(binary);
            return copy.toBin().size();
        });
        bench("string.construct", bytes, 1, iterations, [&](int) {
            ve::Var copy(text);
            return copy.type();
        });
    }
    ve::Var::ListV list(16, ve::Var(std::string(1024, 'x')));
    const ve::Var list_value(list);
    bench("list16.copy", 16384, 1, 50000, [&](int) {
        ve::Var copy(list_value);
        return copy.toList().size();
    });
    std::cerr << "sizeof_Var=" << sizeof(ve::Var) << ",checksum=" << checksum.load() << '\n';
}
