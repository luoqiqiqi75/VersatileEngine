#include "imol/logmanager.h"

#include <QVariant>
#include <QFile>
#include <QFileInfo>
#include <QDir>

using namespace imol;

QLog::QLog(const QString &color_str, const QString &level_str, const QString &detail_str) :
    m_q_debug(qDebug().nospace().noquote())
{
    m_q_debug << color_str << level_str;
    if (!detail_str.isEmpty()) m_q_debug << detail_str;
}

QLog::~QLog()
{
    m_q_debug << "\033[0m";
}

QLog & QLog::space()
{
    m_q_debug.setAutoInsertSpaces(true);
    return *this;
}

QLog & QLog::operator << (const QString &str)
{
    m_q_debug << str.toStdString().c_str();
    return *this;
}

QLog & QLog::operator << (const QVariant &var)
{
    m_q_debug << var.toString().toStdString().c_str();
    return *this;
}

QLog & QLog::operator << (const void *ptr)
{
    m_q_debug << ptr;
    return *this;
}

FLog::FLog(const QString &level_str, const QString &detail_str, const QString &file_name_prefix, const QString &file_name_suffix) :
    m_f_debug(QDebug(&m_log_file).nospace().noquote())
{
    QString file_name = QString("%1_%2.%3").arg(file_name_prefix, QDateTime::currentDateTime().toString("yyyyMMdd"), file_name_suffix);
    QFileInfo file_info(file_name);
    if (!file_info.dir().exists()) file_info.dir().mkpath(file_info.dir().absolutePath());
    m_log_file.setFileName(file_name);
    if (!m_log_file.open(QIODevice::Append)) return;

    m_f_debug << level_str;
    if (!detail_str.isEmpty()) m_f_debug << detail_str;
}

FLog::~FLog()
{
    if (!m_log_file.isOpen()) return;
    m_f_debug << '\n';
    m_log_file.close();
}

FLog & FLog::space()
{
    m_f_debug.setAutoInsertSpaces(true);
    return *this;
}

FLog & FLog::operator << (const QString &str)
{
    if (m_log_file.isOpen()) m_f_debug << str.toStdString().c_str();
    return *this;
}

FLog & FLog::operator << (const QVariant &var)
{
    if (m_log_file.isOpen()) m_f_debug << qPrintable(var.toString());
    return *this;
}

FLog & FLog::operator << (const void *ptr)
{
    if (m_log_file.isOpen()) m_f_debug << ptr;
    return *this;
}

QFLog::QFLog(const QString &color_str, const QString &level_str, const QString &detail_str,
             const QString &file_name_prefix, const QString &file_name_suffix) :
    m_q_log(color_str, level_str, detail_str),
    m_f_log(level_str, detail_str, file_name_prefix, file_name_suffix)
{
}

QFLog & QFLog::space()
{
    m_q_log.space();
    m_f_log.space();
    return *this;
}

QFLog & QFLog::operator << (const QString &str)
{
    m_q_log << str;
    m_f_log << str;
    return *this;
}

QFLog & QFLog::operator << (const QVariant &var)
{
    m_q_log << var;
    m_f_log << var;
    return *this;
}

QFLog & QFLog::operator << (const void *ptr)
{
    m_q_log << ptr;
    m_f_log << ptr;
    return *this;
}
