#pragma once

#include <ve/rtt/loop_object.h>

#ifdef IMOL_HAS_OROCOS_RTT

#include <rtt/TaskContext.hpp>
#include <rtt/Activity.hpp>

namespace imol {

class RttLoopObject : public LoopObject {
public:
    explicit RttLoopObject(const std::string& name, double period_sec = 0.001);
    ~RttLoopObject() override;

    void addTask(const Task& t) override;
    bool isRunning() const override;
    int taskCount() const override;
    bool start() override;
    bool stop() override;

    double period() const { return m_period; }
    void executePendingTasks();

private:
    double m_period;
    std::atomic<bool> m_running;
    mutable std::mutex m_mutex;
    List<Task> m_pending_tasks;
};

} // namespace imol

#endif // IMOL_HAS_OROCOS_RTT
