#include "terminal.h"
#include "ui_terminal.h"

#include "core/commandmanager.h"
#include "core/networkmanager.h"
#include "core/translationmanager.h"

#include <QFontDatabase>
#include <QKeyEvent>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QXmlStreamReader>
#include <QScrollBar>
#include <QTimer>
#include <QMutex>
#include <QDialog>
#include <QGridLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QThread>
#include <QProcess>

#define DTR(Str) translator().dtr("imol", Str)

using namespace imol;

//class VersionCommand : public imol::BaseCommand
//{
//public:
//    explicit VersionCommand(QObject *parent = nullptr) : imol::BaseCommand("version", false, parent) {}

//    QString usage() const override { return DTR("version <path> [-s]"); }
//    QString instruction() const override { return DTR("Show registered version"); }

//protected:
//    void complete(imol::ModuleObject *mobj, const QString &text) override;
//    void hint(imol::ModuleObject *mobj, const QString &text) override;
//    void run(imol::ModuleObject *mobj, const QString &param) override;
//};

//void VersionCommand::complete(imol::ModuleObject *, const QString &text)
//{
//    if (text.endsWith(IMOL_MODULE_NAME_SEPARATOR)) return;
//    QString last = text.section(IMOL_MODULE_NAME_SEPARATOR, 0, -2);
//    QString half = text.section(IMOL_MODULE_NAME_SEPARATOR, -1);
//    if (half.length() == 0) return;
//    if (last.isEmpty()) {
//        foreach (imol::ModuleObject *mobj, ve::version::manager().keyMobj()->cmobjs()) {
//            if (mobj->name().startsWith(half)) {
//                emit input(mobj->name().right(mobj->name().length() - half.length()));
//                break;
//            }
//        }
//        return;
//    }
//    auto tar = ve::version::manager().keyMobj(last);
//    if (tar->isEmptyMobj()) return;
//    foreach (imol::ModuleObject *mobj, tar->cmobjs()) {
//        if (mobj->name().startsWith(half)) {
//            emit input(mobj->name().right(mobj->name().length() - half.length()));
//            break;
//        }
//    }
//}

//void VersionCommand::hint(imol::ModuleObject *, const QString &text)
//{
//    QString last = text.section(IMOL_MODULE_NAME_SEPARATOR, 0, -2);
//    QString half = text.section(IMOL_MODULE_NAME_SEPARATOR, -1);
//    QStringList keys;
//    if (last.isEmpty()) {
//        foreach (const QString &key, ve::version::manager().keyMobj()->cmobjNames()) {
//            if (key.startsWith(half)) keys.append(key);
//        }
//    } else {
//        auto tar = ve::version::manager().keyMobj(last);
//        if (tar->isEmptyMobj()) return;
//        foreach (const QString &key, tar->cmobjNames()) {
//            if (key.startsWith(half)) keys.append(key);
//        }
//    }
//    if (!keys.isEmpty()) emit output(keys.join("\t") + '\n');
//}

//void VersionCommand::run(imol::ModuleObject *, const QString &param)
//{
//    if (param.contains("-s")) {
//        QString rpath = param;
//        rpath.replace(" -s", "");
//        rpath = rpath.trimmed();
//        emit output(DTR("version: %1").arg(ve::version::number(rpath, true)));
//    } else if (param.contains("-r")) {
//        QString rpath = param;
//        rpath.replace(" -r", "");
//        rpath = rpath.trimmed();
//        emit output(DTR("version: %1").arg(ve::version::releaseString(rpath)));
//    } else {
//        emit output(DTR("version: %1").arg(ve::version::number(param, false)));
//    }
//}

QString g_mono_font_family = "Cascadia Code";

Highlighter::Highlighter(const QStringList &cmds, bool enable_path, QTextDocument *parent): QSyntaxHighlighter(parent),
    m_enable_path(enable_path)
{
    m_logo_format.setForeground(QColor(88, 88, 88));
    m_logo_format.setBackground(QColor(16, 16, 16));
    m_logo_format.setFontFamily(g_mono_font_family); // Source Code Pro
    m_logo_format.setFontWeight(QFont::ExtraBold);
    m_logo_format.setFontPointSize(5);
    m_logo_format.setFontLetterSpacing(100);

    m_topic_format.setForeground(QColor(144, 233, 233));
    m_topic_format.setFontWeight(QFont::Bold);

    m_keyword_format.setForeground(Qt::yellow);

    foreach (const QString &cmd, cmds) {
        addKey(cmd);
    }

    if (enable_path) {
        m_path_format.setForeground(QBrush(QColor(0, 153, 204)));
        m_path_expression = QRegularExpression("^(?:\\w.*)?>");
    }

    m_json_format.setForeground(QBrush(QColor(204, 204, 204)));
//    m_json_format.setFontItalic(true);

    m_xml_format.setForeground(QBrush(QColor(204, 204, 204)));
    m_xml_format.setFontItalic(true);
}

void Highlighter::addKey(const QString &key)
{
    HighlightingRule rule;
    rule.pattern = QRegularExpression(QString("^\\s*\\b%1\\b").arg(key));
    rule.format = m_keyword_format;
    m_rules.insert(key, rule);
}

void Highlighter::removeKey(const QString &key)
{
    m_rules.remove(key);
}

void Highlighter::highlightBlock(const QString &text)
{
//    DLOG << "[" << text << "]" << currentBlockState();

    //logo line
    if (text.startsWith("<<  ") && text.endsWith("  >>")) {
        setFormat(0, text.length(), m_logo_format);
        return;
    }

    //topic line
    if (text.startsWith(DTR("VersatileEngine Terminal"))) {
        setFormat(0, text.length(), m_topic_format);
        return;
    }

    //empty line
    if (text.trimmed().isEmpty()) {
        setCurrentBlockState(-1);
        return;
    } else {
        setCurrentBlockState(previousBlockState());
    }

    //log
    if (text.startsWith("[I|")) {
        setFormat(0, text.count(), Qt::darkCyan);
        return;
    } else if (text.startsWith("[W|")) {
        setFormat(0, text.count(), Qt::darkYellow);
        return;
    } else if (text.startsWith("[E|")) {
        setFormat(0, text.count(), Qt::darkRed);
        return;
    } else if (text.startsWith("[S|")) {
        setFormat(0, text.count(), Qt::darkMagenta);
        return;
    } else if (text.startsWith("[d|")) {
        setFormat(0, text.count(), Qt::darkGreen);
        return;
    } else if ((text.startsWith("{") || text.startsWith("["))) {
        setCurrentBlockState(2);
    }

    //json
    if (currentBlockState() == 2 || previousBlockState() == 2) {
        setFormat(0, text.length(), m_json_format);
        return;
    }

    //xml
    if (text.startsWith("<?")) setCurrentBlockState(3);
    if (currentBlockState() == 3 || previousBlockState() == 3) {
        setFormat(0, text.length(), m_xml_format);
        return;
    }

    int offset = 0;

    //path
    if (m_enable_path) {
        QRegularExpressionMatchIterator matchIterator = m_path_expression.globalMatch(text, offset);
        if (matchIterator.hasNext()) {
            offset += matchIterator.next().capturedLength() + 1;
            setFormat(0, offset, m_path_format);

            //cmd line
            setCurrentBlockState(1);
        } else {
            //normal line
            setCurrentBlockState(0);
        }
    } else {
        //cmd line
        setCurrentBlockState(1);
    }

    //keyword
    if (currentBlockState() == 1/* && previousBlockState() < 0*/) {
        QStringRef text_ref(&text, offset, text.length() - offset);
        for (const auto &rule : qAsConst(m_rules)) {
            QRegularExpressionMatchIterator matchIterator = rule.pattern.globalMatch(text_ref);
            if (matchIterator.hasNext()) {
                QRegularExpressionMatch match = matchIterator.next();
                setFormat(offset + match.capturedStart(), match.capturedLength(), rule.format);
            }
        }
    }
}

const QStringList slogans = {
    "A vision of a better building.",
    "A Vision To Clear Solutions.",
    "Adding engineering to next built.",
    "Adding value in every building.",
    "Adorn your home today.",
    "Alien ideas. Human code.",
    "Arrival of the fittest.",
    "Better process.",
    "Bringing your ideas and innovations to life.",
    "Building Better Communities With You.",
    "Building better development.",
    "Building dream reality.",
    "Building dreams.",
    "Building rise with us.",
    "Building The Future On A Foundation of Excellence.",
    "Building the future with ideas.",
    "Building the future.",
    "Choosing the best skills from the rest.",
    "Civil And Structural Engineering Solutions.",
    "Civil Engineering, Professional Enough.",
    "Committed to build better.",
    "Committed To Excellence.",
    "Concrete is our language.",
    "Create. Enhance. and Sustain.",
    "Currently impacting the industry.",
    "Delivering excellence in living.",
    "Delivering excellence.",
    "Delivering results, reliability, & rock solid dependability.",
    "Design it and build it.",
    "Designing future with excellence.",
    "Designing the Future, Today.",
    "Developers of universe.",
    "Developing roads of success.",
    "Discover and Develop.",
    "Discovering possibility in concrete.",
    "Draw on passion.",
    "Engineering for customer needs.",
    "Engineering That Works.",
    "Engineering the better way.",
    "Engineering the world differently.",
    "Engineering the world, civilly.",
    "Engineering with new perspectives.",
    "Engineering With Style.",
    "Engineering with value.",
    "Engineering your dreams with us.",
    "Engineering. Surveying. Solutions.",
    "Engineers create the world.",
    "Enhancing the future.",
    "Excellence and innovation built into every design.",
    "Excellence in every experience.",
    "Exceptional Service Exceeding Expectations.",
    "Experience. Precision. Excellence.",
    "Exploring new possibilities.",
    "Extended efficiency.",
    "Facets of involvement.",
    "Feel high, build right.",
    "Finding real world solutions.",
    "Follow your ambition.",
    "Future is under construction.",
    "Giving solutions to your works.",
    "Green navigation and sustainability.",
    "Hard as concrete. Flexible as steel.",
    "If God Didn't Build It, An Engineer Did!",
    "Impossible is nothing.",
    "In the world of renewable energy… We cast quite a shadow.",
    "Ingenuity for life.",
    "Innovative products and services for aerospace and defense.",
    "Inventing for life.",
    "It's whats inside that counts.",
    "Let us help you invest in sustainable infrastructure.",
    "Let's build a future.",
    "Lets you shine with skill.",
    "Machine learning for particle accelerators.",
    "Make science your obedient servant.",
    "Making it happen.",
    "Passion meets the future.",
    "Passion that builds excellence.",
    "People. Planet. Profit.",
    "Precision in every task.",
    "Problem solved.",
    "Professional enough.",
    "Professional. Innovative. Reliable.",
    "Real People. Real Work. Real Rewards.",
    "Real world solutions.",
    "Reliable building, reliable quality.",
    "Reliable engineering takes many forms.",
    "Renewable energy realization.",
    "Resourceful. Naturally.",
    "Responsible for better building.",
    "Securing the world – bit by bit.",
    "See It, Solve It!",
    "See the world the way we build.",
    "Seeing what doesn't exist yet. That's our strength.",
    "Simply certified.",
    "Smart solutions in full system design.",
    "Sometimes you need a little help from below.",
    "Sound quality. Sound engineering.",
    "The impossible just takes a bit longer.",
    "The power of applied intelligence.",
    "There is no impossible.",
    "There is nothing civil about these Engineers.",
    "Think big. We do.",
    "Together we build the future.",
    "Trusted Quality for Over 20 Years.",
    "Trusted quality since (year).",
    "Truth in engineering.",
    "Turning ideas into reality.",
    "Way better than just engineering.",
    "We build solutions.",
    "We build the legacies.",
    "We build, we rule.",
    "We Create The World.",
    "We made passion our raw material.",
    "We Resolve Your Land Development Issues.",
    "We take a closer look.",
    "When you need experience, we have it covered.",
    "Where tradition meets innovation.",
    "Who really knows renewable energy? The answer is blowing in the wind.",
    "Yes, we can.",
    "You dream it, we build it!",
    "You light engineering.",
    "You See the world the way we Build.",
    "Your structure, our excellence.",
    "Your toolkit for business creativity."
};

Terminal::Terminal(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::Terminal),
    m_is_root(false),
    m_mutex(new QMutex),
    m_double_tab(false),
    m_cur_path(""),
    m_past_cmd_cache_index(-1),
    m_wait_timer(new QTimer(this)),
    m_handle_log(false),
    m_handle_name("")
{
    qRegisterMetaType<QTextCursor>("QTextCursor");
    qsrand(QDateTime::currentMSecsSinceEpoch());

    setAttribute(Qt::WA_StyledBackground);

    QFontDatabase database;
    auto font_families = database.families();
    if (!font_families.contains(g_mono_font_family)) {
        for (const auto& ff : font_families) {
            if (ff.contains("Mono", Qt::CaseInsensitive)) {
                g_mono_font_family = ff;
                break;
            }
        }
    }

    ui->setupUi(this);

    setWindowTitle(DTR("VE Terminal - %1").arg(qApp->applicationName()));

    ui->textBrowser->document()->setMaximumBlockCount(1024);
    ui->textEdit->document()->setMaximumBlockCount(256);
    ui->textEdit->installEventFilter(this);
    ui->textEdit->setFocus();

    connect(m_wait_timer, &QTimer::timeout, this, [this] {
        m_wait_timer->stop();
        analyseCommand("");
    });

    //internal commands
    m_exec_hash.insert("hello", &Terminal::execHello);
    m_exec_hash.insert("help", &Terminal::execHelp);
    m_exec_hash.insert("m", &Terminal::execM);
    m_exec_hash.insert("c", &Terminal::execC);
    m_exec_hash.insert("r", &Terminal::execR);
    m_exec_hash.insert("cd", &Terminal::execR);
    m_exec_hash.insert("p", &Terminal::execP);
    m_exec_hash.insert("names", &Terminal::execNames);
    m_exec_hash.insert("ls", &Terminal::execNames);
    m_exec_hash.insert("g", &Terminal::execGet);
    m_exec_hash.insert("get", &Terminal::execGet);
    m_exec_hash.insert("s", &Terminal::execSet);
    m_exec_hash.insert("set", &Terminal::execSet);
    m_exec_hash.insert("insert", &Terminal::execInsert);
    m_exec_hash.insert("remove", &Terminal::execRemove);
    m_exec_hash.insert("import", &Terminal::execImport);
    m_exec_hash.insert("export", &Terminal::execExport);
    m_exec_hash.insert("wait", &Terminal::execWait);
    m_exec_hash.insert("sh", &Terminal::execSh);
    m_exec_hash.insert("history", &Terminal::execHistory);
    m_exec_hash.insert("log", &Terminal::execLog);
    m_exec_hash.insert("cp", &Terminal::execCp);
    m_exec_hash.insert("clear", &Terminal::execClear);
    m_exec_hash.insert("reorder", &Terminal::execReorder);
    m_exec_hash.insert("su", &Terminal::execSu);
    m_exec_hash.insert("process", &Terminal::execProcess);
//    command().regist(new VersionCommand(this));

    Highlighter *input_highlighter = new Highlighter(m_exec_hash.keys(), false, ui->textEdit->document());
    Highlighter *output_highlighter = new Highlighter(m_exec_hash.keys(), true, ui->textBrowser->document());

    //external commands
    QStringList cmd_keys = command().keys();
    cmd_keys.sort();
    foreach (QString cmd_key, cmd_keys) {
        connect(command().get(cmd_key), &imol::BaseCommand::message, this, &Terminal::msg);
        m_external_exec_keys.append(cmd_key);
        input_highlighter->addKey(cmd_key);
        output_highlighter->addKey(cmd_key);
    }

    connect(&command(), &imol::CommandManager::commandAdded, this, [=] (const QString &key) {
        connect(command().get(key), &imol::BaseCommand::message, this, &Terminal::msg);
        m_external_exec_keys.append(key);
        input_highlighter->addKey(key);
        output_highlighter->addKey(key);
    });
    connect(&command(), &imol::CommandManager::commandRemoved, this, [=] (const QString &key) {
        m_external_exec_keys.removeOne(key);
        input_highlighter->removeKey(key);
        output_highlighter->removeKey(key);
    });

    //initialization
    QStringList logo;
    logo.append(R"(  .                 .                       )");
    logo.append(R"( / \               / \                      )");
    logo.append(R"( \  \             /  /-----------.          )");
    logo.append(R"(  \  \           /  /  o----------'         )");
    logo.append(R"(   \  \         /  / \  \                   )");
    logo.append(R"(    \  \       /  /   \  \                  )");
    logo.append(R"(     .  o     o  .     .  \_______.-.       )");
    logo.append(R"(      \  \   /  /       \  ,-------`-'      )");
    logo.append(R"(       \  \ /  /         \  \               )");
    logo.append(R"(        \  v  /           \  \              )");
    logo.append(R"(         \   /             \  `----------.  )");
    logo.append(R"(          `-'               `-------------' )");
    logo.append(R"(                                            )");

    ui->textBrowser->append("");
    for (const QString &logo_line : logo) {
        ui->textBrowser->append("<<  " + logo_line + "  >>");
    }
    ui->textBrowser->append("");

//    ui->textBrowser->append(DTR("VE Termial %1 (License created at %2)").arg(2024).arg(m("ve.lic._date")->getString()));
    ui->textBrowser->append(DTR("VersatileEngine Terminal %1").arg(2024));
    ui->textBrowser->append(DTR("Type \"help [-d]\" for more information"));
    ui->textBrowser->append("");

    QString init_cmd = "hello engineer";
    ui->textBrowser->append("> " + init_cmd);
    analyseCommand(init_cmd);
    finishCommand(init_cmd);

    // external control
    imol::ModuleObject *terminal_mobj = m().regist(this, "ve.terminal");
    connect(terminal_mobj->append(this, "show"), &imol::ModuleObject::changed, this, [this] (const QVariant &var) {
        if (var.toBool()) {
            show();
        } else {
            close();
        }
    });
}

Terminal::~Terminal()
{
    if (m_handle_log) qInstallMessageHandler(0);

    delete ui;
    delete m_mutex;
    delete m_wait_timer;
}

Terminal & Terminal::instance()
{
    static Terminal manager;
    return manager;
}

void Terminal::out(const QString &text)
{
    if (m_handle_name.isEmpty()) {
        m_mutex->lock();
        ui->textBrowser->append(text);
        m_mutex->unlock();
        return;
    }

    if (text.isEmpty()) return;

    net(m_handle_name)->writeRaw(QString("%1\r").arg(text).toUtf8());
}

void Terminal::error(const QString &text)
{
    out(text);
}

void Terminal::msg(const QString &text)
{
    if (m_msg_handle_names.isEmpty()) return out(text);

    if (text.isEmpty()) return;

    foreach (QString msg_handle_name, m_msg_handle_names) {
        imol::NetworkObject *msg_nobj = net(msg_handle_name);
        if (msg_nobj && msg_nobj->handle()->isConnected()) msg_nobj->writeRaw(QString("%1\r").arg(text).toUtf8());
    }
}

void Terminal::setOutputHandle(const QString &handle_name)
{
    m_handle_name = handle_name;
}

void Terminal::appendMsgHandle(const QString &handle_name)
{
    m_msg_handle_names.append(handle_name);
}

void Terminal::removeMsgHandle(const QString &handle_name)
{
    m_msg_handle_names.removeOne(handle_name);
}

void Terminal::appendLog(const QString &text)
{
    msg(text.mid(5, text.length() - 9));
}

bool Terminal::analyseCommand(const QString &cmd_text)
{
    QString cur_cmd;
    if (m_wait_timer->isActive()) {
        if (!cmd_text.isEmpty()) m_future_cmd_cache.append(cmd_text);
        return false;
    } else if (m_future_cmd_cache.size() > 0) {
        if (!cmd_text.isEmpty()) m_future_cmd_cache.append(cmd_text);
        cur_cmd = m_future_cmd_cache.takeAt(0);

        //exec next
        m_wait_timer->start(2);
    } else {
        cur_cmd = cmd_text;
    }

    QString cmd = cur_cmd.section(" ", 0, 0).trimmed();
    QString param = cur_cmd.section(" ", 1, -1).trimmed();

    if (cmd.isEmpty()) return true;

    if (m_exec_hash.contains(cmd)) {
        (this->*m_exec_hash.value(cmd))(param);
    } else if (m_external_exec_keys.contains(cmd)) {
        imol::BaseCommand *exec_cmd = command().get(cmd);
        if (exec_cmd) {
            if (exec_cmd->isRoot() && !m_is_root) {
                out(DTR("Permission denied"));
            } else {
                connect(exec_cmd, &imol::BaseCommand::output, this, &Terminal::out);
                connect(exec_cmd, &imol::BaseCommand::error, this, &Terminal::error);
                connect(exec_cmd, &imol::BaseCommand::unknown, this, &Terminal::unknownOption);
                exec_cmd->exec(curMobj(), param);
                disconnect(exec_cmd, &imol::BaseCommand::output, this, &Terminal::out);
                disconnect(exec_cmd, &imol::BaseCommand::error, this, &Terminal::error);
                disconnect(exec_cmd, &imol::BaseCommand::unknown, this, &Terminal::unknownOption);
            }
        } else {
            execUnknown(cmd); //todo
        }
    } else {
        execUnknown(cmd);
    }

    return true;
}

void Terminal::appendInput(const QString &text)
{
    m_mutex->lock();
    ui->textEdit->moveCursor(QTextCursor::End, QTextCursor::MoveAnchor);
    ui->textEdit->insertPlainText(text);
    m_mutex->unlock();
}

void Terminal::unknownOption(const QString &option)
{
    out(DTR("unknown option: %1").arg(option));

    imol::BaseCommand *exec_cmd = qobject_cast<imol::BaseCommand *>(sender());
    if (exec_cmd) out(DTR("usage: %2").arg(exec_cmd->usage()));
}

bool Terminal::eventFilter(QObject *watched, QEvent *event)
{
    if (!(event->type() == QEvent::KeyPress && watched == ui->textEdit)) return false;
    QKeyEvent *key_event = dynamic_cast<QKeyEvent *>(event);
    if (!key_event) return false;

    if (key_event->key() == Qt::Key_Enter || key_event->key() == Qt::Key_Return) {
        QString command = ui->textEdit->document()->toPlainText();
        if (key_event->modifiers() == Qt::ControlModifier) {
            //new line
            ui->textEdit->textCursor().insertText("\n");
            return true;
        } else {
            //exec command
            startCommand(command);
            if (analyseCommand(command)) finishCommand(command);
            return true;
        }
    } else if (key_event->key() == Qt::Key_Tab) {
        QString text = ui->textEdit->document()->toPlainText();
        int space_num = text.count(" ");
        if (space_num == 0) {
            //auto command
            foreach (const QString &cmd, m_exec_hash.keys() + m_external_exec_keys) {
                if (cmd.startsWith(text)) {
                    int separator_index = cmd.indexOf('.', text.length());
                    if (separator_index < 0) {
                        ui->textEdit->setPlainText(cmd + " ");
                    } else {
                        QString sub_cmd = cmd.left(separator_index);
                        ui->textEdit->setPlainText(sub_cmd + " ");
                    }
                    break;
                }
            }
        } else if (space_num == 1) {
            //auto complete
            QString cmd = text.section(" ", 0, 0);
            QString half_param = "";
            QString full_param = "";
            if (cmd == "c") {
                QString half_name = text.section(" ", 1, 1);
                QStringList cmobj_names = curMobj()->cmobjNames();
                foreach (QString cmobj_name, cmobj_names) {
                    if (cmobj_name.startsWith(half_name) && cmobj_name.length() > half_name.length()) {
                        half_param = half_name;
                        full_param = cmobj_name;
                        break;
                    }
                }
            } else if (cmd == "r" || cmd == "m" || cmd == "cd") {
                imol::ModuleObject *mobj = (cmd == "m" ? m().rootMobj() : curMobj());
                QString temp_name = text.section(" ", 1, 1);
                QString half_name = temp_name.section(IMOL_MODULE_NAME_SEPARATOR, -1, -1);
                QString left_name = temp_name.section(IMOL_MODULE_NAME_SEPARATOR, 0, -2);
                QStringList cmobj_names = (left_name.isEmpty() ? mobj->cmobjNames() : mobj->rmobj(left_name)->cmobjNames());
                foreach (QString cmobj_name, cmobj_names) {
                    if (cmobj_name.startsWith(half_name) && cmobj_name.length() > half_name.length()) {
                        half_param = half_name;
                        full_param = cmobj_name;
                        break;
                    }
                }

                if (m_double_tab) {
                    if (cmobj_names.isEmpty()) {
                        out(DTR("no cmobj"));
                    } else {
                        out(QString("%1\n").arg(cmobj_names.join("\t ")));
                    }
                    m_double_tab = false;
                }
            } else if (cmd == "import") {
                half_param = text.section(" ", 1);
                if (half_param.startsWith("j", Qt::CaseInsensitive)) {
                    full_param = "JSON -d=";
                } else if (half_param.startsWith("x", Qt::CaseInsensitive)) {
                    full_param = "XML -d=";
                } else if (half_param.startsWith("b", Qt::CaseInsensitive)) {
                    full_param = "BIN -p=";
                } else {
                    half_param = "";
                }
            } else if (cmd == "export") {
                half_param = text.section(" ", 1);
                if (half_param.startsWith("j", Qt::CaseInsensitive)) {
                    full_param = "JSON";
                } else if (half_param.startsWith("x", Qt::CaseInsensitive)) {
                    full_param = "XML";
                } else if (half_param.startsWith("b", Qt::CaseInsensitive)) {
                    full_param = "BIN";
                } else {
                    half_param = "";
                }
            } else if (m_external_exec_keys.contains(cmd)) {
                imol::BaseCommand *exec_cmd = command().get(cmd);
                if (exec_cmd) {
                    connect(exec_cmd, &imol::BaseCommand::input, this, &Terminal::appendInput);
                    connect(exec_cmd, &imol::BaseCommand::output, this, &Terminal::out);
                    connect(exec_cmd, &imol::BaseCommand::error, this, &Terminal::error);
                    if (m_double_tab) {
                        exec_cmd->autoHint(curMobj(), text.right(text.length() - cmd.length() - 1));
                    } else {
                        exec_cmd->autoComplete(curMobj(), text.right(text.length() - cmd.length() - 1));
                    }
                    disconnect(exec_cmd, &imol::BaseCommand::input, this, &Terminal::appendInput);
                    disconnect(exec_cmd, &imol::BaseCommand::output, this, &Terminal::out);
                    disconnect(exec_cmd, &imol::BaseCommand::error, this, &Terminal::error);
                }
            }

            //automatic completion
            if (!half_param.isEmpty()) {
                text.replace(text.length() - half_param.length(), half_param.length(), full_param);
                ui->textEdit->setPlainText(text);
            } else if (!m_double_tab) {
                m_double_tab = true;
                QTimer::singleShot(200, this, [this] {m_double_tab = false;});
            }
        } else {
            // super commplete
            QString cmd = text.section(" ", 0, 0);

            if (imol::BaseCommand *exec_cmd = command().get(cmd)) {
                connect(exec_cmd, &imol::BaseCommand::input, this, &Terminal::appendInput);
                connect(exec_cmd, &imol::BaseCommand::output, this, &Terminal::out);
                connect(exec_cmd, &imol::BaseCommand::error, this, &Terminal::error);
                if (m_double_tab) {
                    exec_cmd->autoHint(curMobj(), text.right(text.length() - cmd.length() - 1));
                } else {
                    exec_cmd->autoComplete(curMobj(), text.right(text.length() - cmd.length() - 1));
                }
                disconnect(exec_cmd, &imol::BaseCommand::input, this, &Terminal::appendInput);
                disconnect(exec_cmd, &imol::BaseCommand::output, this, &Terminal::out);
                disconnect(exec_cmd, &imol::BaseCommand::error, this, &Terminal::error);
            }

            if (!m_double_tab) {
                m_double_tab = true;
                QTimer::singleShot(200, this, [this] {m_double_tab = false;});
            }
        }

        ui->textEdit->moveCursor(QTextCursor::End, QTextCursor::MoveAnchor);
        return true;
    } else if (key_event->key() == Qt::Key_Up) {
        int index = (m_past_cmd_cache_index < 0 ? m_past_cmd_cache.size() : m_past_cmd_cache_index) - 1;
        if (index < 0 || index >= m_past_cmd_cache.size()) return false;

        ui->textEdit->setPlainText(m_past_cmd_cache.value(index, ""));
        m_past_cmd_cache_index = index;

        ui->textEdit->moveCursor(QTextCursor::End, QTextCursor::MoveAnchor);
        return true;
    } else if (key_event->key() == Qt::Key_Down) {
        int index = (m_past_cmd_cache_index < 0 ? -1 : (m_past_cmd_cache_index + 1 >= m_past_cmd_cache.size() ? -1 : m_past_cmd_cache_index + 1));

        ui->textEdit->setPlainText(index < 0 ? "" : m_past_cmd_cache.value(index, ""));
        m_past_cmd_cache_index = index;

        ui->textEdit->moveCursor(QTextCursor::End, QTextCursor::MoveAnchor);
        return true;
    }

    return false;
}

void Terminal::startCommand(const QString &cmd_text)
{
    ui->textEdit->document()->clear();
    out(DTR("%1> %2").arg(m_cur_path, cmd_text.startsWith("su") ? "su": cmd_text));
}

void Terminal::finishCommand(const QString &cmd_text)
{
    m_past_cmd_cache_index = -1;
    if (m_past_cmd_cache.contains(cmd_text)) m_past_cmd_cache.removeOne(cmd_text);
    m_past_cmd_cache.append(cmd_text.startsWith("su") ? "su": cmd_text);
    if (m_past_cmd_cache.size() > 128) m_past_cmd_cache.removeFirst();

    ui->textBrowser->append("");
    ui->textBrowser->verticalScrollBar()->setValue(ui->textBrowser->verticalScrollBar()->maximum());
}

imol::ModuleObject * Terminal::curMobj()
{
    return m_cur_path.isEmpty() ? m().rootMobj() : m(m_cur_path);
}

void Terminal::execUnknown(const QString &param)
{
    out(DTR("unknown command: %1").arg(param));
}

void Terminal::execHello(const QString &param)
{
    //check param
    if (param == "engineer") {
        ui->textBrowser->append(slogans.at(qrand() % slogans.size()));
        return;
    } if (!param.isEmpty()) {
        unknownOption(param);
        return;
    }

    if (isVisible()) {
        ui->textBrowser->document()->clear();
        out(DTR("VersatileEngine Terminal") + " 2024"); // todo
    } else {
        show();
    }
}

QString strResize(const QString &str, int new_size = 56, char ch = ' ')
{
    return "  " + (str.length() < new_size ? str + QString(new_size - str.length(), ch) + "\t" : str) + " ";
}

void Terminal::execHelp(const QString &param)
{
    //check param
    bool has_details = false;
    if (param == "-d") {
        has_details = true;
    } else if (!param.isEmpty()) {
        unknownOption(param);
        return;
    }

    out(DTR("usage of termial:\n"));
    out(strResize(DTR("hello")));
    out(strResize(DTR("m [<full_name>]")) +
        (has_details ? DTR("Enter module manager") : ""));
    out(strResize(DTR("c <name> | -i=<index>")) +
        (has_details ? DTR("Visit cmobj of current mobj with given name or index") : ""));
    out(strResize(DTR("r | cd <relative_name>")) +
        (has_details ? DTR("Visit rmobj of current mobj with relativ path") : ""));
    out(strResize(DTR("p [<level>]")) +
        (has_details ? DTR("Visit pmobj of current mobj with level") : ""));
    out(strResize(DTR("names | ls")) +
        (has_details ? DTR("Display cmobj names of current mobj") : ""));
    out(strResize(DTR("g | get")) +
        (has_details ? DTR("Get value of current mobj") : ""));
    out(strResize(DTR("s | set [-null | -bool= | -int= | -double=]<value>")) +
        (has_details ? DTR("Set value of current mobj or its rmobj") : ""));
    out(strResize(DTR("cp <full_name> [-nai] [-ar]")) +
        (has_details ? DTR("Copy from given full name") : ""));
    out(strResize(DTR("clear [-nad] [-ov]")) +
        (has_details ? DTR("Clear current item") : ""));
    out(strResize(DTR("insert <name>")) +
        (has_details ? DTR("Insert a mobj with given name") : ""));
    out(strResize(DTR("remove <name>")) +
        (has_details ? DTR("Remove the mobj with given name") : ""));
    out(strResize(DTR("reorder -from=<index> -to=<index> | -names=<name1,name2,...>")) +
        (has_details ? DTR("Reorder item from index to another index, or with given names") : ""));
    out(strResize(DTR("import <JSON | XML | BIN> <-d=<text> | -p=<path>>")) +
        (has_details ? DTR("Import data string with specific type") : ""));
    out(strResize(DTR("export <JSON | XML | BIN> [-c] [-p=<path>]")) +
        (has_details ? DTR("Export current mobj with specific type, option -c is compact") : ""));
    out(strResize(DTR("wait <msec>")) +
        (has_details ? DTR("Wait for a period and cache future commands") : ""));
    out(strResize(DTR("sh [-v] <path>")) +
        (has_details ? DTR("Run shell file with given path") : ""));
    out(strResize(DTR("history [count]")) +
        (has_details ? DTR("List last commands, meaningless will be ignored") : ""));
    out(strResize(DTR("log [-d | --disable]")) +
        (has_details ? DTR("Install LOG message handler") : ""));
    out(strResize(DTR("su [-user=<user_name> -passwd=<password>]")) +
        (has_details ? DTR("Enter root mode with given login info, you can also use dialog to input by input no parameters") : ""));
    out(strResize(DTR("process [-w] <program> [params]")) +
        (has_details ? DTR("(root) Start a local program process, with -w for waiting output, note inside space \" \" must be replaced with \"\\ \"") : ""));

    out(strResize("----"));
    foreach (const QString &key, m_external_exec_keys) {
        imol::BaseCommand *exec_cmd = command().get(key);
        if (exec_cmd) out(strResize(exec_cmd->usage()) + (has_details ? (exec_cmd->isRoot() ? "(root) " + exec_cmd->instruction() : exec_cmd->instruction()) : ""));
    }
}

void Terminal::execM(const QString &param)
{
    if (param.isEmpty()) {
        m_cur_path = "";
    } else if (m(param)->isEmptyMobj()) {
        error(DTR("module not exists"));
        return;
    } else {
        m_cur_path = m(param)->fullName();
    }

    out(DTR("\n%1>").arg(m_cur_path));
}

void Terminal::execC(const QString &param)
{
    if (param.startsWith("-i=")) {
        int index = param.rightRef(param.length() - 3).toInt();
        if (curMobj()->cmobj(index)->isEmptyMobj()) {
            error(DTR("cmobj not exists"));
        } else {
            m_cur_path = curMobj()->cmobj(index)->fullName();
        }
    } else {
        if (!curMobj()->hasCmobj(param)) {
            error(DTR("cmobj not exists"));
        } else {
            m_cur_path = curMobj()->cmobj(param)->fullName();
        }
    }

    out(DTR("\n%1>").arg(m_cur_path));
}

void Terminal::execR(const QString &param)
{
    if (param == "..") return execP("");

    if (!curMobj()->hasRmobj(param)) {
        error(DTR("rmobj not exists"));
    } else {
        m_cur_path = curMobj()->rmobj(param)->fullName();
    }

    out(DTR("\n%1>").arg(m_cur_path));
}

void Terminal::execP(const QString &param)
{
    //check param
    int level = param.toInt();
    if (!(param.isEmpty() || level > 0)) {
        unknownOption(param);
        return;
    }

    imol::ModuleObject *mobj = curMobj()->pmobj(level);
    if (mobj->isEmptyMobj()) {
        error(DTR("pmobj not exists"));
    } else {
        m_cur_path = (mobj == m().rootMobj() ? "" : mobj->fullName());
    }

    out(DTR("\n%1>").arg(m_cur_path));
}

void Terminal::execNames(const QString &param)
{
    //check param
    if (!param.isEmpty()) {
        unknownOption(param);
        return;
    }

    QString names_str = curMobj()->cmobjNames().join("\t ");
    if (!names_str.isEmpty()) out(names_str);
}

void Terminal::execGet(const QString &param)
{
    //check param
    if (!param.isEmpty()) {
        unknownOption(param);
        return;
    }

    QVariant cur_var = curMobj()->get();
    out(DTR("type: [%1], value: [%2]").arg(cur_var.typeName(), cur_var.toString()));
}

void Terminal::execSet(const QString &param)
{
    //check param

    QVariant new_var = param;
    if (param.contains("=")) {
        QString type_str = param.section("=", 0, 0);
        QString value_str = param.section("=", 1, -1);
        if (type_str == "-int") {
            new_var = value_str.toInt();
        } else if (type_str == "-double") {
            new_var = value_str.toDouble();
        } else if (type_str == "-bool") {
            new_var = value_str.compare("true", Qt::CaseInsensitive) == 0 ? true : false;
        }
    } else if (param == "-null") {
        new_var = QVariant();
    }

    QVariant cur_var = curMobj()->get();
    out(DTR("ori\ttype: [%1], value: [%2]").arg(cur_var.typeName(), cur_var.toString()));
    out(DTR("new\ttype: [%1], value: [%2]").arg(new_var.typeName(), new_var.toString()));

    curMobj()->set(this, new_var);
}

void Terminal::execInsert(const QString &param)
{
    if (curMobj()->insert(this, param)) {
        out(DTR("%1 inserted").arg(param));
    } else {
        out(DTR("insert falied"));
    }
}

void Terminal::execRemove(const QString &param)
{
    if (curMobj()->remove(this, param)) {
        out(DTR("%1 removed").arg(param));
    } else {
        out(DTR("remove falied"));
    }
}

void Terminal::execImport(const QString &param)
{
    if (param.startsWith("JSON -d=", Qt::CaseInsensitive)) {
        QJsonDocument json_doc = QJsonDocument::fromJson(param.section("-d=", 1, -1).toUtf8());
        curMobj()->importFromJson(this, json_doc.isArray() ? QJsonValue(json_doc.array()) : QJsonValue(json_doc.object()));
    } else if (param.startsWith("JSON -p=", Qt::CaseInsensitive)) {
        curMobj()->importFromJson(this, m().readFromJson(param.section("-p=", 1, -1)));
    } else if (param.startsWith("XML -d=", Qt::CaseInsensitive)) {
        curMobj()->importFromXmlStr(this, param.section("-d=", 1, -1), true, true, false);
    } else if (param.startsWith("XML -p=", Qt::CaseInsensitive)) {
        m().readFromXmlFile(this, curMobj(), param.section("-p=", 1, -1));
    } else if (param.startsWith("BIN -d=", Qt::CaseInsensitive)) {
        QString dstr = param.section("-d=", 1, -1);
        dstr.remove(' ');
        curMobj()->importFromBin(this, QByteArray::fromHex(dstr.toUtf8()));
    } else if (param.startsWith("BIN -p=", Qt::CaseInsensitive)) {
        curMobj()->importFromBin(this, m().readFromBin(param.section("-p=", 1, -1)));
    } else {
        out(DTR("unknown option: %1").arg(param));
    }
}

void Terminal::execExport(const QString &param)
{
    bool is_compact = param.contains("-c");
    QString path = param.section("-p=", 1, -1);

    if (path.isEmpty()) {
        if (param.startsWith("JSON", Qt::CaseInsensitive)) {
            QJsonValue json_value = curMobj()->exportToJson(false);
            if (json_value.isArray()) {
                QJsonDocument json_doc(json_value.toArray());
                out(QString::fromUtf8(json_doc.toJson(is_compact ? QJsonDocument::Compact : QJsonDocument::Indented)));
            } else if (json_value.isObject()) {
                QJsonDocument json_doc(json_value.toObject());
                out(QString::fromUtf8(json_doc.toJson(is_compact ? QJsonDocument::Compact : QJsonDocument::Indented)));
            } else {
                QString type_str = "null";
                if (json_value.isBool()) {
                    type_str = "bool";
                } else if (json_value.isDouble()) {
                    type_str = "double";
                } else if (json_value.isString()) {
                    type_str = "string";
                } else if (json_value.isUndefined()) {
                    type_str = "undefined";
                }
                out(DTR("type: [%1], value: [%2]").arg(type_str, json_value.toString()));
            }
        } else if (param.startsWith("XML", Qt::CaseInsensitive)) {
            out(curMobj()->exportToXmlStr(is_compact ? -1 : 4, false));
        } else if (param.startsWith("BIN", Qt::CaseInsensitive)) {
            out(curMobj()->exportToBin(false).toHex(' '));
        } else {
            out(DTR("unknown option: %1").arg(param));
        }
    } else {
        bool success = false;
        if (param.startsWith("JSON", Qt::CaseInsensitive)) {
            success = m().writeToJson(path, curMobj()->exportToJson(false));
        } else if (param.startsWith("XML", Qt::CaseInsensitive)) {
            success = m().writeToXmlFile(path, curMobj(), true, false);
        } else if (param.startsWith("BIN", Qt::CaseInsensitive)) {
            success = m().writeToBin(path, curMobj()->exportToBin(false));
        } else {
            out(DTR("unknown option: %1").arg(param));
            return;
        }
        out(DTR("export to %1 %2").arg(path, success ? "succeed" : "failed"));
    }
}

void Terminal::execWait(const QString &param)
{
    bool ok = false;
    int t = param.toInt(&ok);
    if (!ok || t <= 0) {
        out(DTR("unknown option: %1").arg(param));
        return;
    }

    m_wait_timer->start(t);
}

void Terminal::execSh(const QString &param)
{
    bool is_verbose = param.startsWith("-v");

    QFile f(is_verbose ? param.mid(3, param.length() - 3) : param);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        out(DTR("cannot open sh file: %1").arg(param));
        return;
    }

    while (!f.atEnd()) {
        QString command = f.readLine();
        if (command.isEmpty()) continue;

        if (is_verbose) startCommand(command);
        if (analyseCommand(command) && is_verbose) finishCommand(command);
    }

    f.close();
}

void Terminal::execHistory(const QString &param)
{
    bool ok = false;
    int cnt = param.toInt(&ok);
    QStringList history;
    for (int i = ok ? cnt : 0; i < m_past_cmd_cache.size(); i++) {
        QString cmd = m_past_cmd_cache.at(i);
        if (cmd.startsWith("hello")
            || cmd.startsWith("help")
            || cmd.startsWith("history")
            || cmd.startsWith("sh")) continue;
        history.append(cmd);
    }
    out(history.join("\n"));
}

void msgHandler(QtMsgType, const QMessageLogContext &, const QString &msg)
{
    if (QThread::currentThread() == qApp->thread()) {
        Terminal::instance().appendLog(msg);
    } else {
        QMetaObject::invokeMethod(&Terminal::instance(), "appendLog", Qt::QueuedConnection, Q_ARG(QString, msg));
    }
}

void Terminal::execLog(const QString &param)
{
    bool is_disable = (param == "-d" || param == "--disable");

    if (is_disable != m_handle_log) return;
    m_handle_log = !is_disable;

    out(DTR("log tracking is %1").arg(is_disable ? DTR("off") : DTR("on")));

    qInstallMessageHandler(is_disable ? 0 : msgHandler);
}

void Terminal::execCp(const QString &param)
{
    imol::ModuleObject *src_mobj = m(param.trimmed().section(" ", 0, 0));
    curMobj()->copyFrom(this, src_mobj, !param.contains(" -nai"), param.contains(" -ar"));
}

void Terminal::execClear(const QString &param)
{
    curMobj()->clear(this, !param.contains("-nad"), param.contains("-ov"));
}

void Terminal::execReorder(const QString &param)
{
    if (param.contains("-from", Qt::CaseInsensitive)) {
        int from_index = param.indexOf("-from=", 0, Qt::CaseInsensitive);
        int to_index = param.indexOf("-to=", from_index, Qt::CaseInsensitive);
        bool from_ok = false, to_ok = false;
        int from = param.mid(from_index + 6, to_index - from_index - 6).trimmed().toInt(&from_ok);
        int to = param.right(param.length() - to_index - 4).trimmed().toInt(&to_ok);
        if (!from_ok || !to_ok || from < 0 || to < 0) {
            error(DTR("invalid from index or to index"));
        }
        if (!curMobj()->reorder(this, from, to)) out(DTR("reorder failed"));
    } else if (param.contains("-names", Qt::CaseInsensitive)) {
        QStringList rnames = param.section("-names=", -1, -1).split(",");
        if (!curMobj()->reorder(this, rnames)) out(DTR("reorder failed"));
    } else {
        error(DTR("unknown option: %1").arg(param));
    }
}

void Terminal::execSu(const QString &param)
{
    if (m_is_root && param.isEmpty()) {
        ui->lblPrompt->setText(">");
        m_is_root = false;
        out(DTR("root user logout"));
        return;
    }

    QString user, passwd;
    if (param.isEmpty() && this->isVisible()) {
        QDialog dlg(this);
        QGridLayout *grid_layout = new QGridLayout(&dlg);
        grid_layout->addWidget(new QLabel(DTR("User"), &dlg), 0, 0);
        grid_layout->addWidget(new QLabel(DTR("Password"), &dlg), 1, 0);
        QLineEdit *user_edt = new QLineEdit(&dlg);
        grid_layout->addWidget(user_edt, 0, 1);
        QLineEdit *passwd_edt = new QLineEdit(&dlg);
        passwd_edt->setEchoMode(QLineEdit::Password);
        grid_layout->addWidget(passwd_edt, 1, 1);
        QHBoxLayout *h_layout = new QHBoxLayout;
        h_layout->addStretch();
        QPushButton *ok_btn = new QPushButton(DTR("OK"), &dlg);
        connect(ok_btn, &QPushButton::clicked, &dlg, [&] {dlg.accept();});
        h_layout->addWidget(ok_btn);
        QPushButton *cancel_btn = new QPushButton(DTR("Cancel"), &dlg);
        connect(cancel_btn, &QPushButton::clicked, &dlg, [&] {dlg.reject();});
        h_layout->addWidget(cancel_btn);
        grid_layout->addLayout(h_layout, 2, 0, 1, 2);
        dlg.setLayout(grid_layout);
        dlg.resize(dlg.sizeHint());
        if (dlg.exec() == QDialog::Rejected) return;

        user = user_edt->text();
        passwd = passwd_edt->text();
    } else {
        user = param.section("-user=", 1, 1).section(" ", 0, 0);
        passwd = param.section("-passwd=", 1, 1).section(" ", 0, 0);
    }

    if (user.isEmpty() || passwd.isEmpty()) {
        out(DTR("empty user or password"));
        return;
    }

//    QString su_key = License::md5(QString("%1~%2").arg(user, passwd).toUtf8(), QByteArray('6', 66));
//    foreach (imol::ModuleObject *su_mobj, m("imol.lic.root")->cmobjs()) {
//        if (su_key == su_mobj->getString()) {
            m_is_root = true;
            ui->lblPrompt->setText("#");
            out(DTR("login to root user"));
            return;
//        }
//    }

    out(DTR("incorrect password attempts"));
}

void Terminal::execProcess(const QString &param)
{
    if (!m_is_root) {
        out(DTR("Permission denied"));
        return;
    }

    bool is_wait = param.startsWith("-w ");
    QString p_temp = is_wait ? param.right(param.length() - 3) : param;
    p_temp.replace("\\ ", "\r!@\r");
    QStringList p_temp_list = p_temp.split(" ");

    QString program = p_temp_list.at(0);
    program.replace("\r!@\r", " ");

    QStringList params;
    for (int i = 1; i < p_temp_list.size(); i++) {
        QString p = p_temp_list.at(i);
        params.append(p.replace("\r!@\r", " "));
    }

    if (program.isEmpty()) {
        out(DTR("empty process"));
        return;
    }

    if (!is_wait) {
        out(DTR("process %1 starts %2").arg(program, QProcess(this).startDetached(program, params) ? "succeed" : "failed"));
    } else {
        QProcess process(this);
        connect(&process, &QProcess::readyRead, this, [&, this] {
            out(QString::fromLocal8Bit(process.readAll()));
        });
        process.start(program, params);
        process.waitForFinished();
        QByteArray std_out = process.readAllStandardOutput();
        if (!std_out.isEmpty()) out(QString::fromLocal8Bit(std_out));
        QByteArray std_err = process.readAllStandardError();
        if (!std_err.isEmpty()) error(QString::fromLocal8Bit(std_err));
    }
}
