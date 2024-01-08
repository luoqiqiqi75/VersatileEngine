#include "config.h"

#include <QSettings>

using namespace imol;

struct ConfigData
{
    imol::ModuleObject *mobj;

    ConfigData() {}
};

Config::Config(QObject *parent) : QObject{parent},
    d(new ConfigData)
{
    d->mobj = m().regist(this, "imol.cfg");
}

Config::~Config()
{
    delete d;
}

void Config::loadIni(const QString &path)
{
    QSettings config(path, QSettings::IniFormat);
    foreach (const QString &key, config.allKeys()) {
        QString path = key;
        path.replace("/", IMOL_MODULE_NAME_SEPARATOR);
        d->mobj->set(this, path, config.value(key));
    }
}

void Config::saveIni(const QString &path)
{
    QSettings config(path, QSettings::IniFormat);
    foreach (auto group_d, d->mobj->cmobjs()) {
        config.beginGroup(group_d->name());
        // 2 layer
        foreach (auto value_d, group_d->cmobjs()) {
            config.setValue(value_d->name(), value_d->get());
        }
        config.endGroup();
    }
}
