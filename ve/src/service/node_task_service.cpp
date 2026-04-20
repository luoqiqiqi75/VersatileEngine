#include "node_task_service.h"

#include "ve/core/node.h"
#include "ve/core/pipeline.h"

#include <atomic>
#include <chrono>
#include <iomanip>
#include <memory>
#include <sstream>

namespace ve {
namespace service {

struct NodeTaskService::Private
{
    Node* root = nullptr;

    std::string generateTaskId() const
    {
        static std::atomic<uint64_t> counter{0};
        auto ts = std::chrono::steady_clock::now().time_since_epoch().count();
        uint64_t id = static_cast<uint64_t>(ts) ^ (counter.fetch_add(1) << 32);
        std::ostringstream oss;
        oss << std::hex << std::setfill('0') << std::setw(16) << id;
        return oss.str();
    }
};

NodeTaskService::NodeTaskService(Node* root)
    : _p(std::make_unique<Private>())
{
    _p->root = root;
}

NodeTaskService::~NodeTaskService() = default;

std::string NodeTaskService::attach(const std::string& cmdKey, const Var& id,
                                    Node* cmdCtx, Pipeline* detached, DoneFn onDone)
{
    if (!_p->root || !detached || !cmdCtx) {
        delete detached;
        delete cmdCtx;
        return {};
    }

    std::string taskId = _p->generateTaskId();
    Node* taskNode = _p->root->at("ve/server/tasks/" + taskId);
    taskNode->set("status", "running");
    taskNode->set("ok", false);
    taskNode->set("cmd", cmdKey);
    if (!id.isNull()) {
        taskNode->at("id")->set(id);
    }

    auto finished = std::make_shared<std::atomic<bool>>(false);
    auto finalize = [this, id, taskId, cmdCtx, detached, onDone, finished](const Result& res) {
        if (finished->exchange(true, std::memory_order_acq_rel)) {
            return;
        }

        const bool ok = res.isSuccess() || res.isAccepted();
        Node* taskNode = _p->root ? _p->root->find("ve/server/tasks/" + taskId) : nullptr;
        if (taskNode) {
            taskNode->set("status", ok ? "done" : "error");
            taskNode->set("ok", ok);
            if (ok) {
                taskNode->at("result")->set(res.content());
            } else {
                taskNode->set("error", res.content().toString());
                taskNode->set("code", res.code());
            }
        }

        if (onDone) {
            Node event("event");
            event.set("event", "task.result");
            if (!id.isNull()) {
                event.at("id")->set(id);
            }
            event.set("task_id", taskId);
            event.set("ok", ok);
            if (ok) {
                event.at("data")->set(res.content());
            } else {
                event.set("code", std::to_string(res.code()));
                event.set("error", res.content().toString());
            }
            onDone(event);
        }

        delete detached;
        delete cmdCtx;
    };

    detached->setResultHandler([finalize](const Result& res) {
        finalize(res);
    });

    const auto state = detached->state();
    if (state == Pipeline::DONE || state == Pipeline::ERRORED || state == Pipeline::IDLE) {
        finalize(detached->lastResult());
    }

    return taskId;
}

} // namespace service
} // namespace ve
