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
                                    Pipeline& pipeline, DoneFn onDone)
{
    if (!_p->root) {
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

    Node* root = _p->root;
    auto finished = std::make_shared<std::atomic<bool>>(false);
    auto finalize = [root, id, taskId, onDone, finished](Pipeline& pipe) {
        if (finished->exchange(true, std::memory_order_acq_rel)) {
            return;
        }

        const Result& res = pipe.lastResult();
        const bool ok = res.isSuccess() || res.isAccepted();
        Node* taskNode = root ? root->find("ve/server/tasks/" + taskId) : nullptr;
        if (taskNode) {
            taskNode->set("status", ok ? "done" : "error");
            taskNode->set("ok", ok);
            if (ok) {
                if (Node* reply = pipe.context() ? pipe.context()->find("reply") : nullptr) {
                    taskNode->at("result")->copy(reply, true, true, true);
                } else {
                    taskNode->at("result")->set(Var());
                }
            } else {
                taskNode->set("error", res.message);
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
                if (Node* reply = pipe.context() ? pipe.context()->find("reply") : nullptr) {
                    event.at("data")->copy(reply, true, true, true);
                } else {
                    event.at("data")->set(Var());
                }
            } else {
                event.set("error", res.message);
            }
            onDone(event);
        }
    };

    // Completion is reported through the pipeline callback (no signals).
    pipeline.onFinished([finalize](Pipeline& pipe) { finalize(pipe); });

    const auto state = pipeline.state();
    if (state == Pipeline::DONE || state == Pipeline::ERRORED) {
        finalize(pipeline);
    }

    return taskId;
}

} // namespace service
} // namespace ve
