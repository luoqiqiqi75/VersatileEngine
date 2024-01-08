#include "ve/core/data.h"

namespace ve {

namespace data {

Manager& manager() { return imol::m(); }

Data* create(QObject* context, const QString& path)
{
    auto d = imol::m(path);
    return d->isEmptyMobj() ? imol::m().regist(context, path) : d;
}

bool free(QObject* context, const QString& path)
{
    return imol::m().cancel(context, path);
}

Data* at(const QString& path)
{
    return imol::m(path);
}

DataListener* listenerAt(const QString& path)
{
    auto d = imol::m(path);
    return d->isEmptyMobj() ? nullptr : d;
}

}

Data* d(const QString& path) { return data::at(path); }

}

