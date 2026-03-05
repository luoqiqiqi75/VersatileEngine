#pragma once

#include <ve/rtt/result.h>
#include <ve/rtt/meta.h>

#include <functional>

namespace imol {

class CommandObject;
class LoopObject;

class Procedure {
public:
    using Functor = std::function<Result(CommandObject*)>;

    Procedure() : m_f(nullptr), m_loop(nullptr), m_block(false) {}
    Procedure(const Functor& f, LoopObject* loop = nullptr, bool block = false)
        : m_f(f), m_loop(loop), m_block(block) {}

    const Functor& f() const { return m_f; }
    LoopObject* loop() const { return m_loop; }
    bool isBlocking() const { return m_block; }

    void setLoop(LoopObject* l) { m_loop = l; }
    void setBlocking(bool b) { m_block = b; }

    explicit operator bool() const { return m_f != nullptr; }

    static Procedure fromVoid(const std::function<void()>& f, LoopObject* l = nullptr) {
        return Procedure([f](CommandObject*) -> Result { f(); return Result::SUCCESS; }, l);
    }

    static Procedure fromResult(const std::function<Result()>& f, LoopObject* l = nullptr) {
        return Procedure([f](CommandObject*) -> Result { return f(); }, l);
    }

    static Procedure fromCommand(const Functor& f, LoopObject* l = nullptr) {
        return Procedure(f, l);
    }

    template<typename F>
    static Procedure fromData(F f, const std::string& data_name = "_input", LoopObject* l = nullptr);

    template<typename F, typename T>
    static Procedure fromT(F f, const T& t, LoopObject* l = nullptr) {
        return Procedure([f, t](CommandObject*) -> Result { return f(t); }, l);
    }

private:
    Functor m_f;
    LoopObject* m_loop;
    bool m_block;
};

using Proc = Procedure;

template<typename F>
struct FuncEnum {
    using Info = FInfo<F>;
    enum {
        IsValid = Info::IsFunction,
        IsVoidF = (Info::ArgCnt == 0),
        IsDataF = (Info::ArgCnt == 1),
    };
};

} // namespace imol
