#include "core/commandmanager.h"

namespace imol {

BaseCommand::BaseCommand(const QString &key, bool is_root, QObject *parent) : QObject(parent),
    m_key(key),
    m_is_root(is_root)
{

}

QString BaseCommand::key() const
{
    return m_key;
}

bool BaseCommand::isRoot() const
{
    return m_is_root;
}

QString BaseCommand::usage() const
{
    return "";
}

QString BaseCommand::instruction() const
{
    return usage();
}

void BaseCommand::autoComplete(ModuleObject *mobj, const QString &text)
{
    complete(mobj, text);
}

void BaseCommand::autoHint(ModuleObject *mobj, const QString &text)
{
    hint(mobj, text);
}

void BaseCommand::exec(ModuleObject *mobj, const QString &param)
{
    run(mobj, param);
    emit finished();
}

void BaseCommand::complete(ModuleObject *mobj, const QString &text)
{
    Q_UNUSED(mobj)
    Q_UNUSED(text)
}

void BaseCommand::hint(ModuleObject *mobj, const QString &param)
{
    Q_UNUSED(mobj)
    Q_UNUSED(param)
}

void BaseCommand::run(ModuleObject *mobj, const QString &param)
{
    Q_UNUSED(mobj)
    Q_UNUSED(param)
}

CommandManager::CommandManager(QObject *parent) : QObject(parent)
{
}

CommandManager::~CommandManager()
{
}

CommandManager & CommandManager::instance()
{
    static CommandManager manager;
    return manager;
}

bool CommandManager::regist(BaseCommand *cmd)
{
    if (!cmd || m_cmds.contains(cmd->key())) return false;
    m_cmds.insert(cmd->key(), cmd);
    emit commandAdded(cmd->key());
    return true;
}

bool CommandManager::cancel(const QString &key)
{
    if (!m_cmds.contains(key)) return false;
    emit commandRemoved(key);
    m_cmds.remove(key);
    return true;
}

QStringList CommandManager::keys() const
{
    return m_cmds.keys();
}

BaseCommand * CommandManager::get(const QString &key)
{
    return m_cmds.value(key, nullptr);
}

//output function
CommandManager & command()
{
    return CommandManager::instance();
}

}
