#include "ve/qt/qt_entry.h"

#include "ve/entry.h"
#include "ve/core/log.h"
#include "ve/core/loop.h"
#include "ve/core/module.h"
#include "ve/core/node.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QIODevice>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QThread>
#include <QTimer>
#include <QWidget>
#include <QtGlobal>

#ifdef VE_QT_HAS_IMOL
#  include "imol/logmanager.h"
#  include "ve/qt/imol_legacy.h"
#endif

namespace ve::qt {

namespace {

Node* qtConfigNode()
{
    Node* r = node::root();
    if (!r) {
        return nullptr;
    }
    return r->find("ve/qt/config");
}

class QtOperationRecorder : public QObject
{
public:
    explicit QtOperationRecorder(QObject* parent) : QObject(parent) {}

protected:
    bool eventFilter(QObject* o, QEvent* e) override
    {
        static bool event_lock = false;
        auto lock_f = [] {
            if (event_lock) {
                return false;
            }
            event_lock = true;
            QTimer::singleShot(0, [] { event_lock = false; });
            return true;
        };
        switch (e->type()) {
            case QEvent::MouseButtonPress:
            case QEvent::MouseButtonDblClick:
                if (qobject_cast<QWidget*>(o) && lock_f()) {
                    if (auto ab = qobject_cast<QAbstractButton*>(o)) {
                        if (ab->text().isEmpty()) {
                            qInfo() << "<rescue.record> [BTN]" << dynamic_cast<QMouseEvent*>(e) << o;
                        } else {
                            qInfo().nospace() << "<rescue.record> [BTN " << ab->text() << "] "
                                              << dynamic_cast<QMouseEvent*>(e) << " " << o;
                        }
                    } else {
                        qInfo() << "<rescue.record>" << dynamic_cast<QMouseEvent*>(e) << o;
                    }
                }
                break;
            case QEvent::KeyPress:
                if (qobject_cast<QWidget*>(o) && lock_f()) {
                    qInfo() << "<rescue.record>" << dynamic_cast<QKeyEvent*>(e) << o;
                }
                break;
            default: break;
        }
        return false;
    }
};

static void installQtMessageHandlerIfNeeded(Node* cfg)
{
    bool on = true;
    if (cfg) {
        if (Node* n = cfg->find("log/install_qt_message_handler")) {
            on = n->getBool(true);
        }
    }
    if (!on) {
        return;
    }

#if defined(Q_OS_ANDROID) && defined(VE_QT_HAS_IMOL)
    static QtMessageHandler default_handler = nullptr;
    default_handler = qInstallMessageHandler(
        [](QtMsgType type, const QMessageLogContext& context, const QString& msg) {
            switch (type) {
                case QtDebugMsg:
                    imol::FLog(LOG_LEVEL_STR_DEBUG, "", LOG_FILE_NAME_PREFIX, LOG_FILE_NAME_SUFFIX) << msg;
                    break;
                case QtInfoMsg:
                    imol::FLog(LOG_LEVEL_STR_INFO, "", LOG_FILE_NAME_PREFIX, LOG_FILE_NAME_SUFFIX) << msg;
                    break;
                case QtWarningMsg:
                    imol::FLog(LOG_LEVEL_STR_WARNING, "", LOG_FILE_NAME_PREFIX, LOG_FILE_NAME_SUFFIX) << msg;
                    break;
                case QtCriticalMsg:
                    imol::FLog(LOG_LEVEL_STR_ERROR, "", LOG_FILE_NAME_PREFIX, LOG_FILE_NAME_SUFFIX) << msg;
                    break;
                case QtFatalMsg:
                    imol::FLog(LOG_LEVEL_STR_SUDO, "", LOG_FILE_NAME_PREFIX, LOG_FILE_NAME_SUFFIX) << msg;
                    break;
            }
            if (default_handler) {
                default_handler(type, context, msg);
            }
        });
#else
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& msg) {
        QString thread_info = (QThread::currentThread() == QCoreApplication::instance()->thread())
            ? QStringLiteral("M")
            : QString::number(reinterpret_cast<unsigned long long>(QThread::currentThreadId()) & 0xffffffff, 16);
        switch (type) {
            case QtDebugMsg:
                veLogD << thread_info.toStdString() << ") " << msg.toLocal8Bit().constData();
                break;
            case QtInfoMsg:
                veLogI << thread_info.toStdString() << ") " << msg.toStdString();
                break;
            case QtWarningMsg:
                veLogW << thread_info.toStdString() << ") " << msg.toStdString();
                break;
            case QtCriticalMsg:
                veLogE << thread_info.toStdString() << ") " << msg.toStdString();
                break;
            case QtFatalMsg:
                veLogS << thread_info.toStdString() << ") " << msg.toStdString();
                break;
        }
    });
#endif
}

} // namespace

void applyEarlySettings()
{
    Node* cfg = qtConfigNode();

    bool share_gl = true;
    bool use_96 = QCoreApplication::testAttribute(Qt::AA_Use96Dpi);
    if (cfg) {
        if (Node* n = cfg->find("attr/ShareOpenGLContexts")) {
            share_gl = n->getBool(true);
        }
        if (Node* n = cfg->find("attr/Use96Dpi")) {
            use_96 = n->getBool(QCoreApplication::testAttribute(Qt::AA_Use96Dpi));
        }
    }
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts, share_gl);
    QCoreApplication::setAttribute(Qt::AA_Use96Dpi, use_96);

    if (!cfg) {
        return;
    }

    if (Node* env = cfg->find("env")) {
        for (Node* ch : *env) {
            const std::string& k = ch->name();
            if (k.empty()) {
                continue;
            }
            std::string v = ch->getString("");
            qputenv(k.c_str(), QByteArray(v.data(), int(v.size())));
        }
    }

    if (Node* prep_root = cfg->find("path/prepend")) {
#if defined(Q_OS_WIN)
        const char* env_key = "PATH";
        const char sep = ';';
#else
        const char* env_key = "LD_LIBRARY_PATH";
        const char sep = ':';
#endif
        QString full = QString::fromLocal8Bit(qgetenv(env_key));
        QString current_dir = QDir().absolutePath();
        for (Node* path_n : *prep_root) {
            QString env_path = QString::fromStdString(path_n->getString(""));
            if (env_path.startsWith('.')) {
                env_path.replace(0, 1, current_dir);
            }
            env_path.replace('/', QDir::separator());
            if (!full.isEmpty()) {
                full.append(sep);
            }
            full.append(env_path);
        }
        qputenv(env_key, full.toLocal8Bit());
    }
}

class QtModule : public Module
{
    QCoreApplication* app_ = nullptr;

public:
    explicit QtModule(const std::string& name) : Module(name) {}

protected:
    void init() override
    {
        Node* cfg = node()->find("config");

        std::string app_type = "gui";
        if (cfg) {
            if (Node* atn = cfg->find("app_type")) {
                app_type = atn->getString("gui");
            }
        }

        applyEarlySettings();

        auto& opts = entry::options();
        int argc = opts.argc;
        if (app_type == "widgets") {
            app_ = new QApplication(argc, opts.argv);
        } else {
            app_ = new QGuiApplication(argc, opts.argv);
        }

        loop::setMainRunner(
            [this]() -> int { return app_->exec(); },
            [this](int) { app_->quit(); }
        );

        installQtMessageHandlerIfNeeded(cfg);

#ifdef VE_QT_HAS_IMOL
        bool bridge_signals = true;
        if (cfg) {
            if (Node* n = cfg->find("log/bridge_imol_log_signals")) {
                bridge_signals = n->getBool(true);
            }
        }
        if (bridge_signals) {
            QObject::connect(imol::legacy::d(QStringLiteral("ve.log.debug")), &imol::ModuleObject::changed,
                [](const QVariant& var, const QVariant&, QObject*) { qDebug() << var.toString(); });
            QObject::connect(imol::legacy::d(QStringLiteral("ve.log.info")), &imol::ModuleObject::changed,
                [](const QVariant& var, const QVariant&, QObject*) { qInfo() << var.toString(); });
            QObject::connect(imol::legacy::d(QStringLiteral("ve.log.warning")), &imol::ModuleObject::changed,
                [](const QVariant& var, const QVariant&, QObject*) { qWarning() << var.toString(); });
            QObject::connect(imol::legacy::d(QStringLiteral("ve.log.error")), &imol::ModuleObject::changed,
                [](const QVariant& var, const QVariant&, QObject*) { qCritical() << var.toString(); });
        }

        bool bridge_export = true;
        if (cfg) {
            if (Node* n = cfg->find("log/bridge_imol_log_export")) {
                bridge_export = n->getBool(true);
            }
        }
        if (bridge_export) {
            QObject::connect(imol::legacy::d(QStringLiteral("ve.log.export")), &imol::ModuleObject::changed,
                [](const QVariant&, const QVariant&, QObject*) {
                auto files_d = imol::legacy::d(QStringLiteral("ve.log.export.files"));
                files_d->clear(nullptr);
                QDir ld("log");
                for (auto fi : ld.entryInfoList(QDir::Files)) {
                    QFile f(fi.absoluteFilePath());
                    if (f.open(QIODevice::ReadOnly)) {
                        files_d->append(nullptr, fi.baseName())->set(QString::fromUtf8(f.readAll()));
                        f.close();
                    }
                }
            });
        }

        QObject::connect(imol::legacy::d(QStringLiteral("ve.rescue.record")), &imol::ModuleObject::changed,
            [](const QVariant& v, const QVariant&, QObject*) {
                static QtOperationRecorder r(nullptr);
                if (!qApp) {
                    return;
                }
                if (v.toBool()) {
                    qApp->installEventFilter(&r);
                } else {
                    qApp->removeEventFilter(&r);
                }
            });
#else
        if (cfg && qApp) {
            if (Node* n = cfg->find("rescue/record_ui_events")) {
                if (n->getBool(false)) {
                    static QtOperationRecorder r2(nullptr);
                    qApp->installEventFilter(&r2);
                }
            }
        }
#endif
    }

    void deinit() override
    {
        delete app_;
        app_ = nullptr;
    }
};

} // namespace ve::qt

VE_REGISTER_PRIORITY_MODULE(ve.qt, ve::qt::QtModule, 5, 1)
