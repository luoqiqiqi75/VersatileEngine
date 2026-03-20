// ----------------------------------------------------------------------------
// imol_legacy.h — dot-path imol::ModuleObject tree (legacy only; not part of ve API)
// ----------------------------------------------------------------------------
#pragma once

#include <QObject>
#include <QString>
#include <QVariant>

#include "ve/global.h"
#include "imol/modulemanager.h"

#define IMOL_DATA_PATH_SEPARATOR "."

namespace imol::legacy {

template<typename T> inline T path_join(const T& t) { return t; }
template<typename T, typename T1, typename... Ts> inline T path_join(const T& t, const T1& t1, const Ts&... ts)
{
    if (t1 == "") {
        return path_join(t, ts...);
    }
    return t + IMOL_DATA_PATH_SEPARATOR + path_join(t1, ts...);
}

VE_API imol::ModuleObject* mobj_create(QObject* context, const QString& path);
VE_API bool mobj_free(QObject* context, const QString& path);
VE_API imol::ModuleObject* mobj_at(const QString& path);
VE_API imol::ModuleObject* mobj_listener_at(const QString& path);

inline imol::ModuleManager& manager() { return imol::m(); }

VE_API imol::ModuleObject* d(imol::ModuleObject* root, const QString& path);
VE_API imol::ModuleObject* d(const QString& path);
inline imol::ModuleObject* d(imol::ModuleObject* root, const std::string& path)
{
    return d(root, QString::fromStdString(path));
}

VE_API bool wait_mobj(imol::ModuleObject* trigger, int timeout_ms = 3000, bool block_input = true);

template<typename T>
inline void diff(imol::ModuleObject* set_d, const T& t)
{
    if (set_d->get().value<T>() != t) {
        set_d->set(QVariant::fromValue(t));
    }
}
inline void diff(imol::ModuleObject* set_d, const char* t)
{
    if (set_d->getString() != t) {
        set_d->set(QString(t));
    }
}
template<typename T>
inline void diff(imol::ModuleObject* set_d, const QString& path, const T& t)
{
    if (set_d->r(path)->get().value<T>() != t) {
        set_d->set(path, QVariant::fromValue(t));
    }
}

template<typename... Args>
inline void on(imol::ModuleObject* target_d, Args&&... args)
{
    QObject::connect(target_d, &imol::ModuleObject::changed, std::forward<Args>(args)...);
}
template<typename... Args>
inline void on(const QString& path, Args&&... args)
{
    QObject::connect(d(path), &imol::ModuleObject::changed, std::forward<Args>(args)...);
}
inline void off(imol::ModuleObject* target_d, QObject* o) { target_d->disconnect(o); }
inline void off(const QString& path, QObject* o) { d(path)->disconnect(o); }

} // namespace imol::legacy

#define IMOL_D(path_literal) [] { static imol::ModuleObject* _p = imol::legacy::d(QStringLiteral(path_literal)); return _p; }()
