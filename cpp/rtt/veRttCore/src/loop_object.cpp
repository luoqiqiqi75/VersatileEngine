#include <ve/rtt/loop_object.h>
#include <ve/rtt/loop_manager.h>

namespace imol {

// --- PoolLoopObject ---

PoolLoopObject::PoolLoopObject(const std::string& name, int thread_count)
    : LoopObject(name)
    , m_running(false)
    , m_thread_count(thread_count) {}

PoolLoopObject::~PoolLoopObject() { stop(); }

void PoolLoopObject::addTask(const Task& t)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tasks.push(t);
    }
    m_cv.notify_one();
}

bool PoolLoopObject::isRunning() const { return m_running; }

int PoolLoopObject::taskCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<int>(m_tasks.size());
}

bool PoolLoopObject::start()
{
    if (m_running) return false;
    m_running = true;
    for (int i = 0; i < m_thread_count; ++i) {
        m_threads.emplace_back([this] { workerLoop(); });
    }
    return true;
}

bool PoolLoopObject::stop()
{
    if (!m_running) return false;
    m_running = false;
    m_cv.notify_all();
    for (auto& t : m_threads) {
        if (t.joinable()) t.join();
    }
    m_threads.clear();
    return true;
}

void PoolLoopObject::workerLoop()
{
    while (m_running) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return !m_running || !m_tasks.empty(); });
            if (!m_running && m_tasks.empty()) return;
            task = std::move(m_tasks.front());
            m_tasks.pop();
        }
        if (task) task();
    }
}

// --- loop::get (Null Object pattern) ---

namespace loop {

LoopObject* get(const std::string& name, bool default_pool)
{
    auto* obj = mgr().get<LoopObject>(name);
    if (obj || !default_pool) return obj;
    static PoolLoopObject* null_obj = nullptr;
    if (!null_obj) {
        null_obj = new PoolLoopObject(IMOL_NULL_OBJECT_NAME);
        null_obj->start();
    }
    return null_obj;
}

} // namespace loop

} // namespace imol
