#ifndef IMOL_COMMANDMANAGER_H
#define IMOL_COMMANDMANAGER_H

#include "core_global.h"

#include <QObject>
#include <QHash>

namespace imol {
class ModuleObject;
class CORESHARED_EXPORT BaseCommand : public QObject
{
    Q_OBJECT

public:
    explicit BaseCommand(const QString &key, bool is_root = false, QObject *parent = nullptr);

    QString key() const;
    bool isRoot() const;

    virtual QString usage() const;
    virtual QString instruction() const;

    void autoComplete(ModuleObject *mobj, const QString &text);
    void autoHint(ModuleObject *mobj, const QString &text);
    void exec(ModuleObject *mobj, const QString &param);

protected:
    virtual void complete(ModuleObject *mobj, const QString &text);
    virtual void hint(ModuleObject *mobj, const QString &text);
    virtual void run(ModuleObject *mobj, const QString &param);

signals:
    void input(const QString &text);
    void output(const QString &text);
    void error(const QString &text);
    void message(const QString &text);

    void unknown(const QString &option);
    void finished();

private:
    QString m_key;
    bool m_is_root;
};

class CORESHARED_EXPORT CommandManager : public QObject
{
    Q_OBJECT

public:
    explicit CommandManager(QObject *parent = nullptr);
    ~CommandManager();
    static CommandManager & instance();

    bool regist(BaseCommand *cmd);
    bool cancel(const QString &key);

    QStringList keys() const;

    BaseCommand * get(const QString &key);

signals:
    void commandAdded(const QString &key);
    void commandRemoved(const QString &key);

private:
    QHash<QString, BaseCommand *> m_cmds;
};
}

//output function
CORESHARED_EXPORT imol::CommandManager & command();

#endif // IMOL_COMMANDMANAGER_H
