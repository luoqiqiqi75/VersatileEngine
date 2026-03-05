#pragma once

#include "ve_rtt_global.h"

namespace imol {

template<typename...> struct TList { typedef void FirstT; };
template<typename T0, typename... T> struct TList<T0, T...> {
    typedef T0 FirstT;
    typedef TList<T...> RestT;
};
template<typename T0, typename T1, typename... T> struct TList<T0, T1, T...> {
    typedef T0 FirstT;
    typedef T1 SecondT;
    typedef TList<T...> RestT;
};

namespace detail {
template<typename T> static decltype(&T::operator()) finfo_test(int);
template<typename T> static void finfo_test(short);
}

template<typename F> struct FInfo : public FInfo<decltype(detail::finfo_test<F>(0))> {};
template<> struct FInfo<void> { enum { IsFunction = false }; };

template<typename Ret, typename... Args>
struct FInfo<Ret(*)(Args...)> {
    typedef Ret RetT;
    typedef TList<Args...> ArgsT;
    typedef Ret (*FptrT)(Args...);
    typedef Ret (*FuncT)(Args...);
    typedef std::function<Ret(Args...)> FunctionT;
    enum { IsFunction = true, IsMember = false, ArgCnt = sizeof...(Args) };
};

template<typename Ret, typename Class, typename... Args>
struct FInfo<Ret(Class::*)(Args...)> {
    typedef Ret RetT;
    typedef Class ClassT;
    typedef TList<Args...> ArgsT;
    typedef Ret (*FptrT)(Args...);
    typedef Ret (Class::*FuncT)(Args...);
    typedef std::function<Ret(Args...)> FunctionT;
    enum { IsFunction = true, IsMember = true, ArgCnt = sizeof...(Args) };
};

template<typename Ret, typename Class, typename... Args>
struct FInfo<Ret(Class::*)(Args...) const> {
    typedef Ret RetT;
    typedef Class ClassT;
    typedef TList<Args...> ArgsT;
    typedef Ret (*FptrT)(Args...);
    typedef Ret (Class::*FuncT)(Args...) const;
    typedef std::function<Ret(Args...)> FunctionT;
    enum { IsFunction = true, IsMember = true, ArgCnt = sizeof...(Args) };
};

template<typename F>
using FuncIn = typename FInfo<F>::ArgsT::FirstT;

std::string _demangle(const char* name);

template<typename T>
struct Meta {
    typedef T TypeT;
    static const char* typeInfoName() { return typeid(T).name(); }
    static std::string typeName() { return _demangle(typeid(T).name()); }
};

namespace basic {

template<class...> using void_t = void;

template<typename L, typename R, class = void>
struct is_comparable : std::false_type {};
template<typename L, typename R>
struct is_comparable<L, R, void_t<decltype(std::declval<L>() == std::declval<R>())>>
    : std::true_type {};

template<typename T>
inline typename std::enable_if<is_comparable<T, T>::value, bool>::type
equals(const T& t1, const T& t2) { return t1 == t2; }

template<typename T>
inline typename std::enable_if<!is_comparable<T, T>::value, bool>::type
equals(const T&, const T&) { return false; }

} // namespace basic

} // namespace imol
