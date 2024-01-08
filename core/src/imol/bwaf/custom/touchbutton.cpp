#include "bwaf/custom/touchbutton.h"

#include <QEvent>
#include <QTouchEvent>
#include <QMouseEvent>
#include <QApplication>

TouchButton::TouchButton(QWidget *parent) : QPushButton(parent),
    m_is_triggered(false)
{
    setAttribute(Qt::WA_AcceptTouchEvents);
    installEventFilter(this);
}

bool TouchButton::eventFilter(QObject *obj, QEvent *e)
{
    switch (e->type()) {
    case QEvent::TouchBegin: case QEvent::MouseButtonPress:
        if (m_is_triggered) break;
        m_is_triggered = true;
        if (!isEnabled()) break;
        emit touchBegun();
        if (e->type() == QEvent::TouchBegin) return true;
        break;
    case QEvent::TouchEnd: case QEvent::TouchCancel: case QEvent::MouseButtonRelease:
    case QEvent::Leave: case QEvent::FocusOut:
        if (!m_is_triggered) break;
        m_is_triggered = false;
        if (!isEnabled()) break;
        emit touchEnded();
        break;
    case QEvent::EnabledChange:
        if (!m_is_triggered) break;
        m_is_triggered = false;
        if (isEnabled()) break;
        emit touchEnded();
        break;
    case QEvent::MouseMove:
        if (QMouseEvent *me = dynamic_cast<QMouseEvent *>(e)) {
            if (rect().contains(me->pos())) break;

            if (!m_is_triggered) break;
            m_is_triggered = false;
            if (!isEnabled()) break;
            emit touchEnded();
            break;
        }
        break;
    case QEvent::TouchUpdate:
        if (QTouchEvent *te = dynamic_cast<QTouchEvent *>(e)) {
            foreach (QTouchEvent::TouchPoint tp, te->touchPoints()) {
                if (rect().contains(tp.pos().toPoint())) continue;

                if (!m_is_triggered) break;
                m_is_triggered = false;
                if (!isEnabled()) break;
                emit touchEnded();
                break;
            }
        }
        break;
    default: break;
    }
    return false;
}
