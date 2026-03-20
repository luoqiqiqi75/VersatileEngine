// ----------------------------------------------------------------------------
// common.h — Qt helpers (ve only: global QObject, qwidget factory, version)
// ----------------------------------------------------------------------------
// imol ModuleObject tree: include "ve/qt/imol_legacy.h" explicitly in legacy code.
// VE lifecycle: ve/entry.h + ve/qt/qt_entry.h
// ----------------------------------------------------------------------------

#pragma once

#include <QString>

#include "ve/global.h"
#include "ve/core/factory.h"
#include "ve/core/log.h"

class QWidget;

namespace ve {

VE_API QObject* global();

} // namespace ve

namespace ve::version {

typedef Factory<int()> Manager;
VE_API Manager& manager();
VE_API int number(const QString& key, bool sum = false);

} // namespace ve::version

#define VE_REGISTER_VERSION(KEY, VER) VE_AUTO_RUN(ve::version::manager().insertOne(#KEY, [] (void) -> int { return VER; }))

#define VE_REGISTER_RELEASE_VERSION(KEY, MAJOR, MINOR, BASELINE) VE_AUTO_RUN( \
    ve::version::manager().regist(QString("@0_%1").arg(KEY), [] (void) -> int { return BASELINE; }); \
    ve::version::manager().regist(QString("@1_%1").arg(KEY), [] (void) -> int { return MAJOR; }); \
    ve::version::manager().regist(QString("@2_%1").arg(KEY), [] (void) -> int { return MINOR; }))

namespace ve::qwidget {

using F = Factory<QWidget*(QWidget*)>;
VE_API F& factory();

} // namespace ve::qwidget

#define VE_REGISTER_QWIDGET(Key, Class) \
    VE_AUTO_RUN(ve::qwidget::factory().insertOne(#Key, [](QWidget* w) -> QWidget* { return new Class(w); });)
