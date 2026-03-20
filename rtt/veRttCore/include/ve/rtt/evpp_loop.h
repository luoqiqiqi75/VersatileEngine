#pragma once

#include <ve/rtt/loop_object.h>

#ifdef IMOL_HAS_EVPP

#include <evpp/event_loop_thread.h>
#include <evpp/event_loop.h>

namespace imol {

class EvppLoopObject : public LoopObject {
public:
    explicit EvppLoopObject(const std::string& name);
    EvppLoopObject(const std::string& name, evpp::EventLoop* current_loop);
    ~EvppLoopObject() override;

    void addTask(const Task& t) override;
    bool isRunning() const override;
    bool start() override;
    bool stop() override;

    evpp::EventLoop* eventLoop() const { return m_loop; }

private:
    evpp::EventLoopThread* m_elt;
    evpp::EventLoop* m_loop;
};

} // namespace imol

#endif // IMOL_HAS_EVPP
