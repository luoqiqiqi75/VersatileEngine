#include <ve/rtt/evpp_loop.h>

#ifdef IMOL_HAS_EVPP

namespace imol {

EvppLoopObject::EvppLoopObject(const std::string& name)
    : LoopObject(name)
    , m_elt(new evpp::EventLoopThread)
    , m_loop(m_elt->loop()) {}

EvppLoopObject::EvppLoopObject(const std::string& name, evpp::EventLoop* current_loop)
    : LoopObject(name)
    , m_elt(nullptr)
    , m_loop(current_loop) {}

EvppLoopObject::~EvppLoopObject()
{
    stop();
    delete m_elt;
}

void EvppLoopObject::addTask(const Task& t)
{
    m_loop->RunInLoop(t);
}

bool EvppLoopObject::isRunning() const
{
    return m_loop && m_loop->IsRunning();
}

bool EvppLoopObject::start()
{
    if (!m_elt) return false;
    m_elt->Start(true);
    return true;
}

bool EvppLoopObject::stop()
{
    if (!m_elt) return false;
    m_elt->Stop(true);
    return true;
}

} // namespace imol

#endif // IMOL_HAS_EVPP
