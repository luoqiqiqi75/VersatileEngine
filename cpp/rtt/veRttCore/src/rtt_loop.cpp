#include <ve/rtt/rtt_loop.h>

#ifdef IMOL_HAS_OROCOS_RTT

namespace imol {

RttLoopObject::RttLoopObject(const std::string& name, double period_sec)
    : LoopObject(name)
    , m_period(period_sec)
    , m_running(false) {}

RttLoopObject::~RttLoopObject() { stop(); }

void RttLoopObject::addTask(const Task& t)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pending_tasks.push_back(t);
}

bool RttLoopObject::isRunning() const { return m_running; }

int RttLoopObject::taskCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<int>(m_pending_tasks.size());
}

bool RttLoopObject::start()
{
    m_running = true;
    return true;
}

bool RttLoopObject::stop()
{
    m_running = false;
    return true;
}

void RttLoopObject::executePendingTasks()
{
    List<Task> tasks;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        tasks.swap(m_pending_tasks);
    }
    for (auto& t : tasks) {
        if (t) t();
    }
}

} // namespace imol

#endif // IMOL_HAS_OROCOS_RTT
