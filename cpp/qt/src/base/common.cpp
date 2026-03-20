#include "ve/qt/core/common.h"

#include <QObject>
#include <QWidget>

VE_REGISTER_VERSION(ve.core, 3)

namespace ve {

QObject* global()
{
    static QObject g;
    return &g;
}

namespace version {

Manager& manager()
{
    static Manager m("ve::version_manager");
    return m;
}

int number(const QString& key, bool sum)
{
    int n = 0;
    if (sum) {
        for (const auto [k, v] : manager()) {
            if (QString::fromStdString(k).startsWith(key) && v) {
                n += v();
            }
        }
    } else {
        if (auto f = manager().value(key.toStdString(), nullptr)) {
            n = f();
        } else {
            n = -1;
        }
    }
    return n;
}

} // namespace version

namespace qwidget {

F& factory()
{
    static F f("ve::qwidget_factory");
    return f;
}

} // namespace qwidget

} // namespace ve
