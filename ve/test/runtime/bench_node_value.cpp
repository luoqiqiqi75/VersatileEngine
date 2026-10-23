// Release benchmark. Run the same executable source against each library build.
// The pre-lock implementation must run without --mixed.
// --mixed enables concurrent readers/writers and requires synchronized values.
// --notify leaves signals enabled (no subscribers); default isolates value cost.
#include "ve/core/node.h"
#include <iomanip>
#include <shared_mutex>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;
constexpr int iterations = 100000;
constexpr int repeats = 7;
std::atomic<uint64_t> checksum{0};

// Pin one worker per physical core of the highest performance class. This
// avoids comparing different P/E core placements on hybrid Windows CPUs.
const std::vector<uintptr_t>& preferred_cpus()
{
    static const auto cpus = [] {
        std::vector<uintptr_t> result;
#ifdef _WIN32
        DWORD bytes = 0;
        GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
        std::vector<unsigned char> buffer(bytes);
        DWORD_PTR allowed = 0, system = 0;
        if (!GetProcessAffinityMask(GetCurrentProcess(), &allowed, &system)
            || !GetLogicalProcessorInformationEx(RelationProcessorCore,
                reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &bytes))
            return result;
        unsigned best_class = 0;
        for (size_t offset = 0; offset < bytes;) {
            const auto* info = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
            const auto& core = info->Processor;
            const auto mask = core.GroupMask[0].Group == 0 ? core.GroupMask[0].Mask & allowed : 0;
            if (mask && core.EfficiencyClass >= best_class) {
                if (core.EfficiencyClass > best_class) result.clear();
                best_class = core.EfficiencyClass;
                result.push_back(mask & (~mask + 1));
            }
            offset += info->Size;
        }
#endif
        return result;
    }();
    return cpus;
}

template<class Fn>
void bench(const char* operation, int bytes, int readers, int writers, Fn operation_fn)
{
    std::vector<double> samples;
    for (int repeat = -1; repeat < repeats; ++repeat) {
        const int workers = readers + writers;
        std::atomic<int> ready{0};
        std::atomic<bool> start{false};
        std::vector<std::thread> threads;
        for (int t = 0; t < workers; ++t) {
            threads.emplace_back([&, t] {
#ifdef _WIN32
                const auto& cpus = preferred_cpus();
                if (!cpus.empty() && !SetThreadAffinityMask(GetCurrentThread(), cpus[t % cpus.size()])) {
                    std::cerr << "Could not pin benchmark worker\n";
                    std::abort();
                }
#endif
                uint64_t sum = 0;
                ready.fetch_add(1, std::memory_order_release);
                while (!start.load(std::memory_order_acquire)) { std::this_thread::yield(); }
                for (int i = 0; i < iterations; ++i) { sum += operation_fn(t, i); }
                checksum.fetch_add(sum, std::memory_order_relaxed);
            });
        }
        while (ready.load(std::memory_order_acquire) != workers) { std::this_thread::yield(); }
        const auto begin = Clock::now();
        start.store(true, std::memory_order_release);
        for (auto& thread : threads) { thread.join(); }
        const double ns = std::chrono::duration<double, std::nano>(Clock::now() - begin).count();
        if (repeat >= 0) samples.push_back(ns / (iterations * workers));
    }
    std::sort(samples.begin(), samples.end());
    const double median = samples[samples.size() / 2];
    std::cout << operation << ',' << bytes << ',' << readers << ',' << writers << ','
              << std::fixed << std::setprecision(2) << median << ',' << 1000.0 / median << '\n';
}
}

int main(int argc, char** argv)
{
    bool mixed = false, notify = false;
    for (int i = 1; i < argc; ++i) {
        mixed |= std::string(argv[i]) == "--mixed";
        notify |= std::string(argv[i]) == "--notify";
    }
    std::cerr << "shared_mutex_bytes=" << sizeof(std::shared_mutex) << ",cpu_masks=";
    for (auto cpu : preferred_cpus()) { std::cerr << std::hex << cpu << ';'; }
    std::cerr << std::dec << '\n';
    std::cout << "operation,bytes,readers,writers,median_ns_per_op,million_ops_per_second\n";
    ve::Node number;
    number.silent(!notify);
    number.set(42);
    ve::Node boolean, real;
    boolean.silent(!notify);
    real.silent(!notify);
    boolean.set(true);
    real.set(1.5);
    std::shared_mutex mutex;
    for (int readers : {1, 4, 8}) {
        bench("getInt", 0, readers, 0, [&](int, int) { return number.getInt(); });
        bench("getBool", 0, readers, 0, [&](int, int) { return boolean.getBool(); });
        bench("getDouble", 0, readers, 0, [&](int, int) { return real.getDouble() == 1.5; });
        bench("shared_lock.only", 0, readers, 0, [&](int, int) {
            std::shared_lock lock(mutex);
            return uint64_t(1);
        });
    }
    const ve::Var integer(42);
    bench("get.toInt", 0, 1, 0, [&](int, int) { return number.get().toInt(); });
    bench("set.const", 0, 0, 1, [&](int, int) {
        number.set(integer);
        return uint64_t(1);
    });
    bench("set.move", 0, 0, 1, [&](int, int) {
        number.set(ve::Var(42));
        return uint64_t(1);
    });
    bench("update.unchanged", 0, 0, 1, [&](int, int) {
        return number.update(integer) ? uint64_t(1) : uint64_t(0);
    });
    if (mixed) {
        bench("int.get.set.concurrent", 0, 4, 1, [&](int t, int i) {
            if (t < 4) return static_cast<uint64_t>(number.getInt());
            number.set(ve::Var(42 + (i & 1)));
            return uint64_t(1);
        });
    }
    const std::vector<ve::Var> types = {ve::Var(), ve::Var(true), ve::Var(42), ve::Var(1.5),
        ve::Var("text"), ve::Var(ve::Bytes{}), ve::Var(ve::Var::ListV{}),
        ve::Var(ve::Var::DictV{}), ve::Var(static_cast<void*>(nullptr)),
        ve::Var(ve::Var::CallableV{}), ve::Var::custom(std::string("custom"))};
    bench("type.trivial.range", 0, 1, 0, [&](int, int i) {
        const auto type = types[static_cast<unsigned>(i) % types.size()].type();
        return type <= ve::Var::DOUBLE || type == ve::Var::POINTER;
    });
    bench("type.trivial.switch", 0, 1, 0, [&](int, int i) {
        switch (types[static_cast<unsigned>(i) % types.size()].type()) {
        case ve::Var::NONE: case ve::Var::BOOL: case ve::Var::INT:
        case ve::Var::DOUBLE: case ve::Var::POINTER: return true;
        default: return false;
        }
    });
    for (int bytes : {8, 16, 256, 4096}) {
        ve::Node node;
        node.silent(!notify);
        const ve::Var payload(std::string(bytes, 'x'));
        node.set(payload);
        for (int readers : {1, 4, 8}) {
            bench("getString", bytes, readers, 0, [&](int, int) {
                auto text = node.getString();
                return text.size() + static_cast<unsigned char>(text.front());
            });
        }
        bench("get.toString", bytes, 1, 0, [&](int, int) {
            return node.get().toString().size();
        });
        bench("getAs.string", bytes, 1, 0, [&](int, int) {
            return node.getAs<std::string>().size();
        });
        bench("set.const", bytes, 0, 1, [&](int, int) {
            node.set(payload);
            return uint64_t(1);
        });
        bench("set.move", bytes, 0, 1, [&](int, int) {
            node.set(ve::Var(std::string(bytes, 'x')));
            return uint64_t(1);
        });
        bench("update.unchanged", bytes, 0, 1, [&](int, int) {
            return node.update(payload) ? uint64_t(1) : uint64_t(0);
        });
        bench("get.set.serial", bytes, 1, 0, [&](int, int) {
            node.set(payload);
            return node.getString().size();
        });
        if (mixed) {
            bench("get.set.concurrent", bytes, 4, 1, [&](int t, int) {
                if (t < 4) return static_cast<uint64_t>(node.getString().size());
                node.set(payload);
                return uint64_t(1);
            });
        }
    }
    std::mutex mtx;
    for (int workers : {1, 4, 8}) {
        bench("mutex.only", 0, workers, 0, [&](int, int) {
            std::lock_guard<std::mutex> lk(mtx);
            return uint64_t(1);
        });
    }
    std::cerr << "mutex_bytes=" << sizeof(std::mutex) << '\n';
    std::cerr << "checksum=" << checksum.load(std::memory_order_relaxed) << '\n';
}
