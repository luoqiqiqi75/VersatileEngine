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

Data* d(Data* root, const QString& path)
{
    auto cd = root->r(path);
    if (cd->isEmptyMobj()) {
        root->insert(nullptr, path);
        cd = root->r(path);
    }
    return cd;
}
Data* d(const QString& path)
{
    return d(imol::m().rootMobj(), path);
}

Data* d(Data* root, const std::string& path) { return d(root, QString::fromStdString(path)); }
Data* d(const std::string& path) { return d(QString::fromStdString(path)); }
Data* d(const char* path) { return d(QString(path)); }
Data* d(Data* root, const char* path) { return d(root, QString(path)); }

}

