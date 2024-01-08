#ifndef IMOL_TERMINAL_H
#define IMOL_TERMINAL_H

#include "ve/core/common.h"

#include <QWidget>
#include <QHash>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>

class QTextBrowser;
class QTextDocument;
struct HighlightingRule
{
    QRegularExpression pattern;
    QTextCharFormat format;
};
//!
//! \brief The Highlighter class
//!
class Highlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    Highlighter(const QStringList &cmds, bool enable_path, QTextDocument *parent = nullptr);

    void addKey(const QString &key);
    void removeKey(const QString &key);

protected:
    void highlightBlock(const QString &text) override;

private:
    bool m_enable_path;

    QHash<QString, HighlightingRule> m_rules;

    QRegularExpression m_path_expression;

    QTextCharFormat m_logo_format;
    QTextCharFormat m_topic_format;
    QTextCharFormat m_keyword_format;
    QTextCharFormat m_path_format;
    QTextCharFormat m_json_format;
    QTextCharFormat m_xml_format;
};

namespace Ui {
class Terminal;
}
class QMutex;
class Terminal;
typedef void (Terminal::*execCmd)(const QString &);
//!
//! \brief The Terminal class
//!
class Terminal : public QWidget
{
    Q_OBJECT

public:
    explicit Terminal(QWidget *parent = nullptr);
    ~Terminal();
    static Terminal & instance();

    void setOutputHandle(const QString &handle_name = "");
    void appendMsgHandle(const QString &handle_name);
    void removeMsgHandle(const QString &handle_name);

    void appendLog(const QString &text);

    bool analyseCommand(const QString &cmd_text);

public slots:
    void out(const QString &text);
    void error(const QString &text);
    void msg(const QString &text);

    void appendInput(const QString &text);
    void unknownOption(const QString &option);

protected:
    bool eventFilter(QObject *watched, QEvent *event);

private:
    void startCommand(const QString &cmd_text);
    void finishCommand(const QString &cmd_text);

    imol::ModuleObject *curMobj();

private slots:
    void execUnknown(const QString &param);
    void execHello(const QString &param);
    void execHelp(const QString &param);
    void execM(const QString &param);
    void execC(const QString &param);
    void execR(const QString &param);
    void execP(const QString &param);
    void execNames(const QString &param);
    void execGet(const QString &param);
    void execSet(const QString &param);
    void execInsert(const QString &param);
    void execRemove(const QString &param);
    void execImport(const QString &param);
    void execExport(const QString &param);
    void execWait(const QString &param);
    void execSh(const QString &param);
    void execHistory(const QString &param);
    void execLog(const QString &param);
    void execCp(const QString &param);
    void execClear(const QString &param);
    void execReorder(const QString &param);
    void execSu(const QString &param);
    void execProcess(const QString &param);

private:
    Ui::Terminal *ui;

    bool m_is_root; //authority control

    QMutex *m_mutex; //mutex for public function

    bool m_double_tab; //double tab pressed

    QString m_cur_path; //current mobj full name

    //past command cache
    QStringList m_past_cmd_cache;
    int m_past_cmd_cache_index;

    //command hash
    QHash<QString, execCmd> m_exec_hash;
    QStringList m_external_exec_keys;

    //future command cache
    QStringList m_future_cmd_cache;

    QTimer *m_wait_timer; //timer for wait command

    bool m_handle_log; //whether take over log handling

    QString m_handle_name; //network handler name
    QStringList m_msg_handle_names;
};

#endif // IMOL_TERMINAL_H
