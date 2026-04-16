#pragma once

#include <ve/rtt/object.h>

namespace imol {

enum LoopType { LOOP_RTT, LOOP_EVPP, LOOP_POOL };

class LoopObject : public Object {
public:
    explicit LoopObject(const std::string& name)
        : Object(name) {}

    virtual ~LoopObject() = default;

    virtual void addTask(const Task& t) = 0;
    virtual bool isRunning() const = 0;
    virtual bool hasTask() const { return taskCount() > 0; }
    virtual int taskCount() const { return 0; }
    virtual bool start() = 0;
    virtual bool stop() = 0;
};

class PoolLoopObject : public LoopObject {
public:
    explicit PoolLoopObject(const std::string& name, int thread_count = 2);
    ~PoolLoopObject() override;

    void addTask(const Task& t) override;
    bool isRunning() const override;
    int taskCount() const override;
    bool start() override;
    bool stop() override;

private:
    void workerLoop();

    std::atomic<bool> m_running;
    int m_thread_count;
    std::vector<std::thread> m_threads;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<Task> m_tasks;
};

} // namespace imol
