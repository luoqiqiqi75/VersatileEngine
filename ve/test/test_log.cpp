// ----------------------------------------------------------------------------
// test_log.cpp — ve::log basic smoke tests (no crash = pass)
// ----------------------------------------------------------------------------

#include "ve_test.h"
#include "ve/core/log.h"

VE_TEST(log_info_no_crash) {
    veLogI << "test info message";
}

VE_TEST(log_warning_no_crash) {
    veLogW << "test warning message";
}

VE_TEST(log_error_no_crash) {
    veLogE << "test error message";
}

VE_TEST(log_spacing_mode_no_crash) {
    veLogIs << "spaced" << "message" << 42;
}

VE_TEST(log_function_call_no_crash) {
    ve::log::i("function call log test");
}

VE_TEST(log_line_no_crash) {
    ve::log::line<>();
}

VE_TEST(log_setAppName_no_crash) {
    ve::log::setAppName("ve_test");
}

VE_TEST(log_ignore_level_compiles) {
    // LogLevel::Ignore should compile to no-op
    ve::LogStream<ve::LogLevel::Ignore, false>() << "this should be ignored" << 42;
    ve::LogStream<ve::LogLevel::Ignore, true>() << "also ignored";
}
