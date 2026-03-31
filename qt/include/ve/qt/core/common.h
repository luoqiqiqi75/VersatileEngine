// ----------------------------------------------------------------------------
// common.h — Qt helpers (ve only: global QObject, qwidget factory, version)
// ----------------------------------------------------------------------------
// IMOL tree helpers live in "ve/qt/imol_legacy.h" when that bridge is needed.
// VE lifecycle entry points are declared in ve/entry.h and ve/qt/qt_entry.h.
// ----------------------------------------------------------------------------

#pragma once

#include <QString>

#include "ve/global.h"
#include "ve/core/factory.h"
#include "ve/core/log.h"

class QWidget;

namespace ve::qwidget {

using F = Factory<QWidget*(QWidget*)>;
VE_API F& factory();

} // namespace ve::qwidget

#define VE_REGISTER_QWIDGET(Key, Class) \
    VE_AUTO_RUN(ve::qwidget::factory().insertOne(#Key, [](QWidget* w) -> QWidget* { return new Class(w); });)
