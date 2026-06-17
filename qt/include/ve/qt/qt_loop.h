// ----------------------------------------------------------------------------
// qt_loop.h - Qt Loop implementations
// ----------------------------------------------------------------------------
#pragma once

#include "ve/core/loop.h"

class QCoreApplication;
class QEventLoop;

namespace ve::qt {

class VE_API QtLoop : public Loop
{
public:
    explicit QtLoop(QEventLoop* loop = nullptr, const std::string& name = "qt");
    ~QtLoop() override;

    void   post(Task task) override;
    bool   isRunning() const override;
    size_t processEvents() override;

private:
    QEventLoop* loop_ = nullptr;
};

class VE_API QtMainLoop : public Loop
{
public:
    explicit QtMainLoop(QCoreApplication* app = nullptr, const std::string& name = "qt.main");
    ~QtMainLoop() override;

    void   post(Task task) override;
    bool   isRunning() const override;
    size_t processEvents() override;

    int    exec() override;   // native QCoreApplication::exec()
    void   quit(int exit_code = 0) override;

private:
    QCoreApplication* app_ = nullptr;
};

} // namespace ve::qt
