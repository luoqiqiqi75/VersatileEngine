#ifndef IMOL_LOGMANAGER_H
#define IMOL_LOGMANAGER_H

#include "core_global.h"

#include <QObject>
#include <QFile>
#include <QDateTime>
#include <QDebug>

#ifndef LOG_FILE_NAME_PREFIX
#ifdef VE_LOG_PREFIX
#define LOG_FILE_NAME_PREFIX QString("./log/%1").arg(VE_LOG_PREFIX)
#else
#define LOG_FILE_NAME_PREFIX QString("./log/global")
#endif
#endif
#ifndef LOG_FILE_NAME_SUFFIX
#define LOG_FILE_NAME_SUFFIX QString("log")
#endif

//!
//! \brief The log level defines are globally used in log defines
//!
#define IMOL_LOG_LEVEL_DEBUG 0
#define IMOL_LOG_LEVEL_INFO 1
#define IMOL_LOG_LEVEL_WARNING 2
#define IMOL_LOG_LEVEL_ERROR 3
#define IMOL_LOG_LEVEL_SUDO 4

//! The min log level can be defined in pro file.
//! \a Q_LEVEL is the log level of console.
//! \a FILE_LEVEL is the log level of log file.
#ifndef LOG_MIN_Q_LEVEL
#define LOG_MIN_Q_LEVEL IMOL_LOG_LEVEL_DEBUG
#endif
#ifndef LOG_MIN_FILE_LEVEL
#define LOG_MIN_FILE_LEVEL IMOL_LOG_LEVEL_INFO
#endif

#if LOG_MIN_Q_LEVEL > IMOL_LOG_LEVEL_DEBUG
#define LOG_NO_Q_DEBUG
#endif
#if LOG_MIN_Q_LEVEL > IMOL_LOG_LEVEL_INFO
#define LOG_NO_Q_INFO
#endif
#if LOG_MIN_Q_LEVEL > IMOL_LOG_LEVEL_WARNING
#define LOG_NO_Q_WARNING
#endif
#if LOG_MIN_Q_LEVEL > IMOL_LOG_LEVEL_ERROR
#define LOG_NO_Q_ERROR
#endif

#if LOG_MIN_FILE_LEVEL > IMOL_LOG_LEVEL_DEBUG
#define LOG_NO_F_DEBUG
#endif
#if LOG_MIN_FILE_LEVEL > IMOL_LOG_LEVEL_INFO
#define LOG_NO_F_INFO
#endif
#if LOG_MIN_FILE_LEVEL > IMOL_LOG_LEVEL_WARNING
#define LOG_NO_F_WARNING
#endif
#if LOG_MIN_FILE_LEVEL > IMOL_LOG_LEVEL_ERROR
#define LOG_NO_F_ERROR
#endif

#ifndef LOG_COLOR_STR_DEBUG
#define LOG_COLOR_STR_DEBUG "\033[32m"
#endif
#ifndef LOG_COLOR_STR_INFO
#define LOG_COLOR_STR_INFO "\033[36m"
#endif
#ifndef LOG_COLOR_STR_WARNING
#define LOG_COLOR_STR_WARNING "\033[33m"
#endif
#ifndef LOG_COLOR_STR_ERROR
#define LOG_COLOR_STR_ERROR "\033[31m"
#endif
#ifndef LOG_COLOR_STR_SUDO
#define LOG_COLOR_STR_SUDO "\033[35m"
#endif

#define LOG_LEVEL_STR_DEBUG QString("[d|%1]").arg(QDateTime::currentDateTime().toString("MM/dd hh:mm:ss.zzz"))
#define LOG_LEVEL_STR_INFO QString("[I|%1]").arg(QDateTime::currentDateTime().toString("MM/dd hh:mm:ss.zzz"))
#define LOG_LEVEL_STR_WARNING QString("[W|%1]").arg(QDateTime::currentDateTime().toString("MM/dd hh:mm:ss.zzz"))
#define LOG_LEVEL_STR_ERROR QString("[E|%1]").arg(QDateTime::currentDateTime().toString("MM/dd hh:mm:ss.zzz"))
#define LOG_LEVEL_STR_SUDO QString("[S|%1]").arg(QDateTime::currentDateTime().toString("MM/dd hh:mm:ss.zzz"))

#ifdef LOG_ENABLE_DETAILS
#define LOG_DETAILS_STR QString("{%1@%2}").arg(__FUNCTION__).arg(__LINE__)
#else
#define LOG_DETAILS_STR QString()
#endif

namespace imol {

/**
 * @brief The QLog class outputs log to console by means of qDebug()
 */
class CORESHARED_EXPORT QLog
{
public:
    explicit QLog(const QString &color_str, const QString &level_str, const QString &detail_str);
    ~QLog();

    QLog & space();

    QLog & operator << (const QString &str);
    QLog & operator << (const QVariant &var);
    QLog & operator << (const void *ptr);
    template<typename T>
    inline QLog & operator << (const T &t)
    {
        m_q_debug << t;
        return *this;
    }

private:
    //copy of qDebug
    QDebug m_q_debug;
};

/**
 * @brief The FLog class outputs log to local files with QDebug()
 */
class CORESHARED_EXPORT FLog
{
public:
    explicit FLog(const QString &level_str, const QString &detail_str, const QString &file_name_prefix, const QString &file_name_suffix);
    ~FLog();

    FLog & space();

    FLog & operator << (const QString &str);
    FLog & operator << (const QVariant &var);
    FLog & operator << (const void *ptr);
    template<typename T>
    inline FLog & operator << (const T &t)
    {
        if (m_log_file.isOpen()) m_f_debug << t;
        return *this;
    }

private:
    //log file
    QFile m_log_file;
    //debug for save text
    QDebug m_f_debug;
};

/**
 * @brief The QFLog class outputs log to both console and file
 */
class CORESHARED_EXPORT QFLog
{
public:
    explicit QFLog(const QString &color_str, const QString &level_str, const QString &detail_str,
                   const QString &file_name_prefix, const QString &file_name_suffix);

    QFLog & space();

    QFLog & operator << (const QString &str);
    QFLog & operator << (const QVariant &var);
    QFLog & operator << (const void *ptr);
    template<typename T>
    inline QFLog & operator << (const T &t)
    {
        m_q_log << t;
        m_f_log << t;
        return *this;
    }

private:
    QLog m_q_log;
    FLog m_f_log;
};
}

//DLOG debug
#if defined(LOG_NO_Q_DEBUG) && defined(LOG_NO_F_DEBUG)
#define DLOG if (false) QNoDebug()
#elif defined(LOG_NO_Q_DEBUG)
#define DLOG imol::FLog(LOG_LEVEL_STR_DEBUG, LOG_DETAILS_STR, LOG_FILE_NAME_PREFIX, LOG_FILE_NAME_SUFFIX)
#elif defined(LOG_NO_F_DEBUG)
#define DLOG imol::QLog(LOG_COLOR_STR_DEBUG, LOG_LEVEL_STR_DEBUG, LOG_DETAILS_STR)
#else
#define DLOG imol::QFLog(LOG_COLOR_STR_DEBUG, LOG_LEVEL_STR_DEBUG, LOG_DETAILS_STR, LOG_FILE_NAME_PREFIX, LOG_FILE_NAME_SUFFIX)
#endif

#define DSLOG (DLOG).space()

//ILOG info
#if defined(LOG_NO_Q_INFO) && defined(LOG_NO_F_INFO)
#define ILOG if (false) QNoDebug()
#elif defined(LOG_NO_Q_INFO)
#define ILOG imol::FLog(LOG_LEVEL_STR_INFO, LOG_DETAILS_STR, LOG_FILE_NAME_PREFIX, LOG_FILE_NAME_SUFFIX)
#elif defined(LOG_NO_F_INFO)
#define ILOG imol::QLog(LOG_COLOR_STR_INFO, LOG_LEVEL_STR_INFO, LOG_DETAILS_STR)
#else
#define ILOG imol::QFLog(LOG_COLOR_STR_INFO, LOG_LEVEL_STR_INFO, LOG_DETAILS_STR, LOG_FILE_NAME_PREFIX, LOG_FILE_NAME_SUFFIX)
#endif

#define ISLOG (ILOG).space()

//WLOG warning
#if defined(LOG_NO_Q_WARNING) && defined(LOG_NO_F_WARNING)
#define WLOG if (false) QNoDebug()
#elif defined(LOG_NO_Q_WARNING)
#define WLOG imol::FLog(LOG_LEVEL_STR_WARNING, LOG_DETAILS_STR, LOG_FILE_NAME_PREFIX, LOG_FILE_NAME_SUFFIX)
#elif defined(LOG_NO_F_WARNING)
#define WLOG imol::QLog(LOG_COLOR_STR_WARNING, LOG_LEVEL_STR_WARNING, LOG_DETAILS_STR)
#else
#define WLOG imol::QFLog(LOG_COLOR_STR_WARNING, LOG_LEVEL_STR_WARNING, LOG_DETAILS_STR, LOG_FILE_NAME_PREFIX, LOG_FILE_NAME_SUFFIX)
#endif

#define WSLOG (WLOG).space()

//ELOG error
#if defined(LOG_NO_Q_ERROR) && defined(LOG_NO_F_ERROR)
#define ELOG if (false) QNoDebug()
#elif defined(LOG_NO_Q_ERROR)
#define ELOG imol::FLog(LOG_LEVEL_STR_ERROR, LOG_DETAILS_STR, LOG_FILE_NAME_PREFIX, LOG_FILE_NAME_SUFFIX)
#elif defined(LOG_NO_F_ERROR)
#define ELOG imol::QLog(LOG_COLOR_STR_ERROR, LOG_LEVEL_STR_ERROR, LOG_DETAILS_STR)
#else
#define ELOG imol::QFLog(LOG_COLOR_STR_ERROR, LOG_LEVEL_STR_ERROR, LOG_DETAILS_STR, LOG_FILE_NAME_PREFIX, LOG_FILE_NAME_SUFFIX)
#endif

#define ESLOG (ELOG).space()

//SLOG
#define SLOG imol::QFLog(LOG_COLOR_STR_SUDO, LOG_LEVEL_STR_SUDO, LOG_DETAILS_STR, LOG_FILE_NAME_PREFIX, LOG_FILE_NAME_SUFFIX)
#define SSLOG (SLOG).space()

#endif // IMOL_LOGMANAGER_H
