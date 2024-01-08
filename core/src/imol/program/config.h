#ifndef IMOL_CONFIG_H
#define IMOL_CONFIG_H

#include "ve/core/data.h"

#ifndef IMOL_CONFIG_FILE
#define IMOL_CONFIG_FILE "config.ini"
#endif

struct ConfigData;
class Config : public QObject
{
    Q_OBJECT

public:
    explicit Config(QObject *parent = nullptr);
    ~Config() override;

    void loadIni(const QString &path = IMOL_CONFIG_FILE);

    void saveIni(const QString &path = IMOL_CONFIG_FILE);

private:
    ConfigData *d;
};

#endif // IMOL_CONFIG_H
