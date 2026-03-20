#include "ve/qt/imol_legacy.h"

#include <QEventLoop>
#include <QTimer>

namespace imol::legacy {

namespace {

imol::ModuleObject* ensure_child(imol::ModuleObject* root, const QString& path)
{
    auto cd = root->r(path);
    if (cd->isEmptyMobj()) {
        root->insert(nullptr, path);
        cd = root->r(path);
    }
    return cd;
}

} // namespace

imol::ModuleObject* mobj_create(QObject* context, const QString& path)
{
    auto d = imol::m(path);
    return d->isEmptyMobj() ? imol::m().regist(context, path) : d;
}

bool mobj_free(QObject* context, const QString& path) { return imol::m().cancel(context, path); }

imol::ModuleObject* mobj_at(const QString& path) { return imol::m(path); }

imol::ModuleObject* mobj_listener_at(const QString& path)
{
    auto d = imol::m(path);
    return d->isEmptyMobj() ? nullptr : d;
}

imol::ModuleObject* d(imol::ModuleObject* root, const QString& path) { return ensure_child(root, path); }

imol::ModuleObject* d(const QString& path) { return ensure_child(imol::m().rootMobj(), path); }

bool wait_mobj(imol::ModuleObject* trigger_d, int timeout_ms, bool block_input)
{
    QEventLoop el;
    QObject::connect(trigger_d, &imol::ModuleObject::changed, &el, &QEventLoop::quit, Qt::DirectConnection);
    QTimer::singleShot(timeout_ms, &el, [&] { el.exit(-1); });
    return el.exec(block_input ? QEventLoop::ExcludeUserInputEvents : QEventLoop::AllEvents) >= 0;
}

} // namespace imol::legacy
