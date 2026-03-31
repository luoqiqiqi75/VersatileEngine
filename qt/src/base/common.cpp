#include "ve/qt/core/common.h"

#include <QObject>
#include <QWidget>

VE_REGISTER_VERSION(ve.core, 3)

namespace ve {

namespace qwidget {

F& factory()
{
    static F f("ve::qwidget_factory");
    return f;
}

} // namespace qwidget

} // namespace ve
