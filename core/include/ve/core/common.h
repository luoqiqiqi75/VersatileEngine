// ----------------------------------------------------------------------------
// common.h
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------

#pragma once

#include "ve/global.h"

#include <QJsonValue>

#include "data.h"
#include "log.h"
#include "factory.h"
#include "terminal.h"

namespace ve {

VE_API QObject* global();

namespace entry {
VE_API void setup(Data* config_d);
VE_API void setup(const QString& config_path, const QString& format = "ini");
VE_API void init();
VE_API void deinit();
}

}

namespace ve::version {
typedef Factory<int()> Manager;
VE_API Manager& manager();
VE_API int number(const QString &key, bool sum = false);
// VE_API QString releaseString(const QString &key);
}

#define VE_REGISTER_VERSION(KEY, VER) VE_AUTO_RUN(ve::version::manager().insertOne(#KEY, [] (void) -> int { return VER; }))

#define VE_REGISTER_RELEASE_VERSION(KEY, MAJOR, MINOR, BASELINE) VE_AUTO_RUN( \
ve::version::manager().regist(QString("@0_%1").arg(KEY), [] (void) -> int { return BASELINE; }); \
ve::version::manager().regist(QString("@1_%1").arg(KEY), [] (void) -> int { return MAJOR; }); \
ve::version::manager().regist(QString("@2_%1").arg(KEY), [] (void) -> int { return MINOR; }))

// QWidget factory
namespace ve::qwidget {
using F = Factory<QWidget*(QWidget*)> ;
VE_API F& factory();
}

#define VE_REGISTER_QWIDGET(Key, Class) VE_AUTO_RUN(ve::qwidget::factory().register(#Key, [] (QWidget* w) -> F* { return new Class(w); });)

// data + Qt utils
namespace ve::data {

template<typename T>
inline void diff(ve::Data* set_d, const T& t) { if (set_d->get().value<T>() != t) set_d->set(QVariant::fromValue(t)); }
inline void diff(ve::Data* set_d, const char* t) { if (set_d->getString() != t) set_d->set(QString(t)); }
template<typename T>
inline void diff(ve::Data* set_d, const QString& path, const T& t) { if (set_d->r(path)->get().value<T>() != t) set_d->set(path, QVariant::fromValue(t)); }

template<typename... Args>
inline void on(Data* target_d, Args&&... args) { QObject::connect(target_d, &ve::Data::changed, std::forward<Args>(args)...); }
template<typename... Args>
inline void on(const QString& path, Args&&... args) { QObject::connect(ve::d(path), &ve::Data::changed, std::forward<Args>(args)...); }
inline void off(Data* target_d, QObject* o) { target_d->disconnect(o); }
inline void off(const QString& path, QObject* o) { ve::d(path)->disconnect(o); }

VE_API bool wait(ve::Data* trigger_d, int timeout = 3000, bool block_input = true);

}
