// ----------------------------------------------------------------------------
// qt_test.h — Qt-side helpers on top of ve_test
// ----------------------------------------------------------------------------
// Qt timers only fire while an event loop is spinning, so the polling helpers
// here pump the event queue instead of just sleeping.
// ----------------------------------------------------------------------------
#pragma once

#include "ve_test.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QEventLoop>

#include <functional>

namespace ve_test {

// Poll a predicate while draining the Qt event queue.
inline bool qt_wait_until(const std::function<bool()>& fn, int timeout_ms = 2000)
{
    QDeadlineTimer deadline(timeout_ms);
    while (!deadline.hasExpired()) {
        if (fn()) return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return fn();
}

// Drain the event queue for a fixed span — for asserting that nothing happens.
inline void qt_settle(int ms = 60)
{
    QDeadlineTimer deadline(ms);
    while (!deadline.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

} // namespace ve_test

#define VE_QT_WAIT(...)  ve_test::qt_wait_until(__VA_ARGS__)
#define VE_QT_SETTLE(...) ve_test::qt_settle(__VA_ARGS__)
