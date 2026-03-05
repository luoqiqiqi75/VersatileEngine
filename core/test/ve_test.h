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

namespace ve_test {

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
