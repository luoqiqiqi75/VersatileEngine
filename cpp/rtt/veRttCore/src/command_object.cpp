#include <ve/rtt/command_object.h>
#include <ve/rtt/loop_object.h>

namespace imol {

CommandObject::CommandObject(const std::string& key)
    : Object(key), m_mgr("_cmd_data") {}

CommandObject::~CommandObject() = default;

void CommandObject::addProc(const Procedure& proc) { m_procedures.push_back(proc); }
void CommandObject::prependProc(const Procedure& proc) { m_procedures.push_front(proc); }

void CommandObject::setResultHandler(const ResultHandler& handler) { m_result_handler = handler; }

Result CommandObject::start(bool auto_delete)
{
    if (m_procedures.empty()) {
        finish(Result::SUCCESS, auto_delete);
        return Result::SUCCESS;
    }

    Procedure proc = m_procedures.front();
    m_procedures.pop_front();

    if (proc.loop()) {
        auto self = this;
        auto ad = auto_delete;
        postToLoop(proc.loop(), [self, proc, ad] {
            Result res = proc.f()(self);
            self->handleResult(res, ad);
        });
        return Result::ACCEPT;
    } else {
        Result res = proc.f()(this);
        if (res.isAccepted()) {
            return Result::ACCEPT;
        } else if (res.isError() || m_procedures.empty()) {
            finish(res, auto_delete);
            return res;
        } else {
            return start(auto_delete);
        }
    }
}

void CommandObject::finish(const Result& result, bool auto_delete)
{
    if (m_result_handler && !result.isAccepted()) {
        m_result_handler(result);
    }
    if (auto_delete) {
        m_finished = true;
    }
}

CommandObject* CommandObject::clone() const
{
    auto* copy = new CommandObject(name());
    copy->m_procedures = m_procedures;
    return copy;
}

void CommandObject::handleResult(const Result& res, bool auto_delete)
{
    if (res.isAccepted()) {
        // awaiting external finish()
    } else if (res.isError() || m_procedures.empty()) {
        finish(res, auto_delete);
    } else if (res.isSuccess()) {
        start(auto_delete);
    }
}

void CommandObject::postToLoop(LoopObject* loop, const Task& task)
{
    if (loop) {
        loop->addTask(task);
    } else {
        task();
    }
}

// --- Global command registry ---

namespace command {

CommandObject* copy(const std::string& key)
{
    auto* tpl = mgr().get<CommandObject>(key);
    return tpl ? tpl->clone() : nullptr;
}

CommandObject* copy(const std::string& key, const CommandObject::ResultHandler& handler)
{
    auto* obj = copy(key);
    if (obj) obj->setResultHandler(handler);
    return obj;
}

} // namespace command

} // namespace imol
