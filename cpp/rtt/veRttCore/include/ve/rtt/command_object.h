#pragma once

#include <ve/rtt/procedure.h>
#include <ve/rtt/data_object.h>

#include <string>

namespace imol {

#define IMOL_COMMAND_INPUT "_input"

class CommandObject : public Object {
public:
    using ResultHandler = std::function<void(const Result&)>;

    explicit CommandObject(const std::string& key = "");
    virtual ~CommandObject();

    // Procedure chain
    void addProc(const Procedure& proc);
    void prependProc(const Procedure& proc);
    const List<Procedure>& procedures() const { return m_procedures; }

    // Private data storage
    template<typename T>
    T data(const std::string& name, bool* ok = nullptr) const {
        auto* obj = m_mgr.get(name);
        if (!obj) { if (ok) *ok = false; return T{}; }
        auto* typed = dynamic_cast<TDataObject<T>*>(obj);
        if (!typed) { if (ok) *ok = false; return T{}; }
        if (ok) *ok = true;
        return typed->get();
    }

    template<typename T>
    bool setData(const std::string& name, const T& t) {
        auto* obj = m_mgr.get(name);
        if (obj) {
            if (auto* typed = dynamic_cast<TDataObject<T>*>(obj)) {
                typed->set(t);
                return true;
            }
            return false;
        }
        auto* dobj = new TDataObject<T>(name);
        dobj->set(t);
        m_mgr.add(dobj);
        return true;
    }

    template<typename T>
    T inputData(bool* ok = nullptr) const { return data<T>(IMOL_COMMAND_INPUT, ok); }

    template<typename T>
    bool setInputData(const T& t) { return setData(IMOL_COMMAND_INPUT, t); }

    // Result handler
    void setResultHandler(const ResultHandler& handler);
    const ResultHandler& resultHandler() const { return m_result_handler; }

    // Execution
    Result start(bool auto_delete = true);
    void finish(const Result& result, bool auto_delete = true);
    bool isFinished() const { return m_finished; }

    CommandObject* clone() const;

private:
    void handleResult(const Result& res, bool auto_delete);
    static void postToLoop(LoopObject* loop, const Task& task);

    Manager m_mgr;
    ResultHandler m_result_handler;
    List<Procedure> m_procedures;
    bool m_finished = false;
};

// Global command registry
namespace command {

inline Manager& mgr() {
    static Manager s_mgr("_imol_command_mgr");
    return s_mgr;
}

CommandObject* copy(const std::string& key);
CommandObject* copy(const std::string& key, const CommandObject::ResultHandler& handler);

} // namespace command

// Procedure::fromData (needs CommandObject definition)
template<typename F>
Procedure Procedure::fromData(F f, const std::string& data_name, LoopObject* l) {
    return Procedure([f, data_name](CommandObject* cobj) -> Result {
        using ArgT = typename FInfo<F>::ArgsT::FirstT;
        bool ok = false;
        auto val = cobj->data<ArgT>(data_name, &ok);
        if (!ok) return Result(Result::FAIL, "missing data: " + data_name);
        return f(val);
    }, l);
}

} // namespace imol
