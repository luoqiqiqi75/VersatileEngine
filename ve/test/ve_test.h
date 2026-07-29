// ----------------------------------------------------------------------------
// ve_test.h — Minimal C++ test framework (no dependencies)
// ----------------------------------------------------------------------------
// Usage:
//   VE_TEST(my_test) { VE_ASSERT_EQ(1 + 1, 2); }
//   int main() { return VE_RUN_ALL(); }
// ----------------------------------------------------------------------------
#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <functional>
#include <sstream>
#include <cmath>
#include <chrono>
#include <thread>

namespace ve_test {

// Poll a predicate until it holds or the deadline passes. For anything driven by
// another thread (loops, timers) — never assert on timing directly.
inline bool wait_until(const std::function<bool()>& fn, int timeout_ms = 1000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (fn()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return fn();
}

inline std::vector<std::function<void()>>& setup_registry() {
    static std::vector<std::function<void()>> fns;
    return fns;
}

struct SetupRegistrar {
    SetupRegistrar(std::function<void()> func) {
        setup_registry().push_back(std::move(func));
    }
};

inline void run_setup() {
    for (auto& fn : setup_registry()) fn();
}

struct TestCase {
    std::string name;
    std::string file;
    int line;
    std::function<void()> func;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

struct Registrar {
    Registrar(const char* name, const char* file, int line, std::function<void()> func) {
        registry().push_back({name, file, line, std::move(func)});
    }
};

struct Failure {
    std::string file;
    int line;
    std::string expr;
};

inline thread_local std::vector<Failure>* current_failures = nullptr;

inline void report_failure(const char* file, int line, const std::string& expr) {
    if (current_failures) current_failures->push_back({file, line, expr});
}

inline int run_all() {
    run_setup();
    int passed = 0, failed = 0;
    std::cout << "\n======== ve_test ========\n\n";

    for (auto& tc : registry()) {
        std::vector<Failure> failures;
        current_failures = &failures;
        try {
            tc.func();
        } catch (const std::exception& e) {
            failures.push_back({tc.file, tc.line, std::string("EXCEPTION: ") + e.what()});
        } catch (...) {
            failures.push_back({tc.file, tc.line, "UNKNOWN EXCEPTION"});
        }
        current_failures = nullptr;

        if (failures.empty()) {
            std::cout << "  PASS  " << tc.name << "\n";
            passed++;
        } else {
            std::cout << "  FAIL  " << tc.name << "\n";
            for (auto& f : failures) {
                std::cout << "        " << f.file << ":" << f.line << "  " << f.expr << "\n";
            }
            failed++;
        }
    }

    std::cout << "\n========================\n"
              << "  " << passed << " passed, " << failed << " failed"
              << " (total " << (passed + failed) << ")\n\n";

    return failed > 0 ? 1 : 0;
}

} // namespace ve_test

// --- Public macros ---

#define VE_TEST(name) \
    static void ve_test_func_##name(); \
    static ve_test::Registrar ve_test_reg_##name(#name, __FILE__, __LINE__, ve_test_func_##name); \
    static void ve_test_func_##name()

#define VE_SETUP(name) \
    static void ve_setup_func_##name(); \
    static ve_test::SetupRegistrar ve_setup_reg_##name(ve_setup_func_##name); \
    static void ve_setup_func_##name()

#define VE_ASSERT(expr) \
    do { if (!(expr)) ve_test::report_failure(__FILE__, __LINE__, "ASSERT( " #expr " )"); } while(0)

#define VE_ASSERT_EQ(a, b) \
    do { \
        auto&& _a = (a); auto&& _b = (b); \
        if (!(_a == _b)) { \
            std::ostringstream _oss; \
            _oss << "ASSERT_EQ( " #a " == " #b " )  got: " << _a << " vs " << _b; \
            ve_test::report_failure(__FILE__, __LINE__, _oss.str()); \
        } \
    } while(0)

#define VE_ASSERT_NE(a, b) \
    do { \
        auto&& _a = (a); auto&& _b = (b); \
        if (_a == _b) { \
            std::ostringstream _oss; \
            _oss << "ASSERT_NE( " #a " != " #b " )  both: " << _a; \
            ve_test::report_failure(__FILE__, __LINE__, _oss.str()); \
        } \
    } while(0)

#define VE_ASSERT_NEAR(a, b, eps) \
    do { \
        auto _a = (a); auto _b = (b); \
        if (std::fabs(_a - _b) > (eps)) { \
            std::ostringstream _oss; \
            _oss << "ASSERT_NEAR( |" #a " - " #b "| <= " #eps " )  got: " << _a << " vs " << _b; \
            ve_test::report_failure(__FILE__, __LINE__, _oss.str()); \
        } \
    } while(0)

#define VE_ASSERT_THROWS(expr) \
    do { \
        bool _threw = false; \
        try { (void)(expr); } catch (...) { _threw = true; } \
        if (!_threw) ve_test::report_failure(__FILE__, __LINE__, "ASSERT_THROWS( " #expr " ) — no exception"); \
    } while(0)

#define VE_RUN_ALL() ve_test::run_all()

#define VE_WAIT(...) ve_test::wait_until(__VA_ARGS__)
