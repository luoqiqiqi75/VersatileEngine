// ----------------------------------------------------------------------------
// qt_loop.h - Qt Loop implementations
// ----------------------------------------------------------------------------
#pragma once

#include "ve/core/loop.h"

class QCoreApplication;
class QEventLoop;

namespace ve::qt {

// QObject timerEvent scheduler shared by both Qt loops (defined in qt_module.cpp).
class QtTimers;

class VE_API QtLoop : public Loop
{
public:
    explicit QtLoop(QEventLoop* loop = nullptr, const std::string& name = "qt");
    ~QtLoop() override;

    void   post(Task task) override;
    size_t processEvents() override;

    TimerHandle addTimer(uint64_t ms, bool repeat, Task tick) override;
    bool        removeTimer(TimerHandle handle) override;

private:
    QEventLoop* loop_ = nullptr;
    std::unique_ptr<QtTimers> timers_;
};

class VE_API QtMainLoop : public Loop
{
public:
    explicit QtMainLoop(QCoreApplication* app = nullptr, const std::string& name = "qt.main");
    ~QtMainLoop() override;

    void   post(Task task) override;
    size_t processEvents() override;

    int    exec() override;   // native QCoreApplication::exec()
    void   quit(int exit_code = 0) override;

    TimerHandle addTimer(uint64_t ms, bool repeat, Task tick) override;
    bool        removeTimer(TimerHandle handle) override;

private:
    QCoreApplication* app_ = nullptr;
    std::unique_ptr<QtTimers> timers_;
};

} // namespace ve::qt
