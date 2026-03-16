// ----------------------------------------------------------------------------
// test_basic_traits.cpp — ve::basic:: type utilities
// ----------------------------------------------------------------------------

#include "ve_test.h"
#include "ve/core/base.h"

using namespace ve;

// --- helpers ---
struct NotComparable { int x; };
struct HasOutput { friend std::ostream& operator<<(std::ostream& os, const HasOutput&) { return os << "HasOutput"; } };
struct NoOutput { int x; };

int free_func(double a, int b) { return static_cast<int>(a) + b; }

struct Klass {
    int method(double x) { return static_cast<int>(x); }
    int const_method(double x) const { return static_cast<int>(x); }
};

// --- is_comparable ---

VE_TEST(is_comparable_int) {
    VE_ASSERT((basic::is_comparable<int, int>::value));
}

VE_TEST(is_comparable_string) {
    VE_ASSERT((basic::is_comparable<std::string, std::string>::value));
}

VE_TEST(is_comparable_not_comparable) {
    VE_ASSERT(!(basic::is_comparable<NotComparable, NotComparable>::value));
}

// --- equals ---

VE_TEST(equals_int_same) {
    VE_ASSERT(basic::equals(42, 42));
}

VE_TEST(equals_int_different) {
    VE_ASSERT(!basic::equals(1, 2));
}

VE_TEST(equals_not_comparable_returns_false) {
    NotComparable a{1}, b{1};
    VE_ASSERT(!basic::equals(a, b));
}

// --- is_outputable / is_inputable ---

VE_TEST(is_outputable_int) {
    VE_ASSERT(basic::is_outputable<int>::value);
}

VE_TEST(is_outputable_string) {
    VE_ASSERT(basic::is_outputable<std::string>::value);
}

VE_TEST(is_outputable_custom) {
    VE_ASSERT(basic::is_outputable<HasOutput>::value);
}

VE_TEST(is_outputable_no_output) {
    VE_ASSERT(!basic::is_outputable<NoOutput>::value);
}

VE_TEST(is_inputable_int) {
    VE_ASSERT(basic::is_inputable<int>::value);
}

VE_TEST(is_inputable_not_inputable) {
    VE_ASSERT(!basic::is_inputable<HasOutput>::value);
}

// --- FnTraits — free function ---

VE_TEST(fntraits_free_func) {
    using F = basic::FnTraits<decltype(&free_func)>;
    VE_ASSERT(F::IsFunction);
    VE_ASSERT(!F::IsMember);
    VE_ASSERT_EQ(F::ArgCnt, 2);
    VE_ASSERT((std::is_same_v<F::RetT, int>));
    VE_ASSERT((std::is_same_v<F::ArgsT::FirstT, double>));
    VE_ASSERT((std::is_same_v<F::ArgsT::SecondT, int>));
    // ArgsTuple + ArgAt
    VE_ASSERT((std::is_same_v<F::ArgsTuple, std::tuple<double, int>>));
    VE_ASSERT((std::is_same_v<F::ArgAt<0>, double>));
    VE_ASSERT((std::is_same_v<F::ArgAt<1>, int>));
}

// --- FnTraits — member function ---

VE_TEST(fntraits_member_func) {
    using F = basic::FnTraits<decltype(&Klass::method)>;
    VE_ASSERT(F::IsFunction);
    VE_ASSERT(F::IsMember);
    VE_ASSERT_EQ(F::ArgCnt, 1);
    VE_ASSERT((std::is_same_v<F::ClassT, Klass>));
    VE_ASSERT((std::is_same_v<F::RetT, int>));
}

VE_TEST(fntraits_const_member) {
    using F = basic::FnTraits<decltype(&Klass::const_method)>;
    VE_ASSERT(F::IsFunction);
    VE_ASSERT(F::IsMember);
}

// --- FnTraits — lambda ---

VE_TEST(fntraits_lambda) {
    auto lam = [](int a, int b) -> double { return a + b; };
    using F = basic::FnTraits<decltype(lam)>;
    VE_ASSERT(F::IsFunction);
    VE_ASSERT_EQ(F::ArgCnt, 2);
    VE_ASSERT((std::is_same_v<F::RetT, double>));
    VE_ASSERT((std::is_same_v<F::ArgsTuple, std::tuple<int, int>>));
}

VE_TEST(fntraits_void) {
    VE_ASSERT(!basic::FnTraits<void>::IsFunction);
}

// --- FnTraits — std::function ---

VE_TEST(fntraits_std_function) {
    using F = basic::FnTraits<std::function<bool(int, std::string)>>;
    VE_ASSERT(F::IsFunction);
    VE_ASSERT_EQ(F::ArgCnt, 2);
    VE_ASSERT((std::is_same_v<F::RetT, bool>));
    VE_ASSERT((std::is_same_v<F::ArgAt<0>, int>));
    VE_ASSERT((std::is_same_v<F::ArgAt<1>, std::string>));
    VE_ASSERT((std::is_same_v<F::ArgsTuple, std::tuple<int, std::string>>));
}

// --- FInfo backward-compat alias ---

VE_TEST(finfo_alias) {
    using F1 = basic::FInfo<decltype(&free_func)>;
    using F2 = basic::FnTraits<decltype(&free_func)>;
    VE_ASSERT((std::is_same_v<F1::RetT, F2::RetT>));
    VE_ASSERT((std::is_same_v<F1::ArgsTuple, F2::ArgsTuple>));
}

// --- _t_remove_rc ---

VE_TEST(remove_rc) {
    VE_ASSERT((std::is_same_v<basic::_t_remove_rc<const int&>, int>));
    VE_ASSERT((std::is_same_v<basic::_t_remove_rc<int&&>, int>));
    VE_ASSERT((std::is_same_v<basic::_t_remove_rc<const std::string&>, std::string>));
}

// --- Meta ---

VE_TEST(meta_type_name_not_empty) {
    auto name = basic::Meta<int>::typeName();
    VE_ASSERT(!name.empty());
}

VE_TEST(meta_type_info_name_not_empty) {
    auto name = basic::Meta<double>::typeIdName();
    VE_ASSERT(name != nullptr);
}

// --- _t_list ---

VE_TEST(t_list_empty) {
    using L = basic::_t_list<>;
    VE_ASSERT((std::is_same_v<L::FirstT, void>));
}

VE_TEST(t_list_one) {
    using L = basic::_t_list<int>;
    VE_ASSERT((std::is_same_v<L::FirstT, int>));
}

VE_TEST(t_list_two) {
    using L = basic::_t_list<int, double>;
    VE_ASSERT((std::is_same_v<L::FirstT, int>));
    VE_ASSERT((std::is_same_v<L::SecondT, double>));
}
