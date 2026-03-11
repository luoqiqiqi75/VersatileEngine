#include "ve/qt/core/common.h"

#include <QThread>

VE_REGISTER_VERSION(ve.core, 3)

#include <QDir>
#include <QSplashScreen>
#include <QPixmap>
#include <QIcon>
#include <QApplication>
#include <QElapsedTimer>
#include <QSettings>
#include <QKeyEvent>
#include <QAbstractButton>
#include <QTimer>
#include <QJsonValue>

// veTerminal (terminal widget + TCP server)
#include "ve/qt/widget/terminal.h"
#include "imol/logmanager.h"
#include "ve/core/rescue.h"

#include "ve/core/module.h"

namespace ve {

QObject* global()
{
    static QObject g;
    return &g;
}

namespace version {

Manager& manager() { static Manager m("ve::version_manager"); return m; }

int number(const QString& key, bool sum)
{
    int n = 0;
    if (sum) {
        for (const auto [k, v] : manager()) {
            if (QString::fromStdString(k).startsWith(key) && v) {
                n += v();
            }
        }
    } else {
        if (auto f = manager().value(key.toStdString(), nullptr)) {
            n = f();
        } else {
            n = -1;
        }
    }
    return n;
}

//QString releaseString(const QString &key)
//{
//    int baseline = manager().create(QString("@0_%1").arg(key));
//    int major = manager().create(QString("@1_%1").arg(key));
//    int minor = manager().create(QString("@2_%1").arg(key));
//    QStringList ver_str_list;
//    for (int i = 3; i < 10; i++) {
//        QString sub_ver_key = QString("@%1_%2").arg(i).arg(key);
//        if (!manager().keyMobj(sub_ver_key)->isEmptyMobj()) ver_str_list.append(QString::number(manager().create(sub_ver_key)));
//    }

//    QString ver_str = QString("v%1.%2.%3").arg(major).arg(minor).arg(number(key, true) - baseline);
//    if (ver_str_list.size() > 0) ver_str.append(".").append(ver_str_list.join("."));
//    return ver_str;
//}

}

namespace qwidget {
F& factory() { static F f("ve::qwidget_factory"); return f; }
}

namespace entry {

class QtOperationRecorder : public QObject
{
public:
    QtOperationRecorder(QObject* parent) : QObject(parent) {}

protected:
    bool eventFilter(QObject* o, QEvent* e) override
    {
        static bool event_lock = false;
        auto lock_f = [] {
            if (event_lock) return false;
            event_lock = true;
            QTimer::singleShot(0, [] { event_lock = false; });
            return true;
        };
        auto log_obj_f = [] (auto debug, QObject* ptr) {
            do {
                debug << " -> " << ptr;
                ptr = ptr->parent();
            } while (ptr);
        };
        switch (e->type()) {
            case QEvent::MouseButtonPress:
            case QEvent::MouseButtonDblClick:
                if (qobject_cast<QWidget*>(o) && lock_f()) {
                    if (auto ab = qobject_cast<QAbstractButton*>(o)) {
                        if (ab->text().isEmpty()) {
                            qInfo() << "<rescue.record> [BTN]" << dynamic_cast<QMouseEvent *>(e) << o;
                        } else {
                            qInfo().nospace() << "<rescue.record> [BTN " << ab->text() << "] " << dynamic_cast<QMouseEvent *>(e) << " " << o;
                        }
                    } else {
                        qInfo() << "<rescue.record>" << dynamic_cast<QMouseEvent *>(e) << o;
                    }
                }
                break;
            case QEvent::KeyPress: {
                if (qobject_cast<QWidget*>(o) && lock_f()) {
                    qInfo() << "<rescue.record>" << dynamic_cast<QKeyEvent*>(e) << o;
                }
            }
                break;
            default: break;
        }
        return false;
    }
};

void ve_data_from_ini(ve::Data* cfg_d, const QString& path) {
    QSettings config(path, QSettings::IniFormat);
    foreach (const QString &key, config.allKeys()) {
        QString new_key = key;
        new_key.replace("/", IMOL_MODULE_NAME_SEPARATOR);
        cfg_d->set(new_key, config.value(key));
    }
}

void ve_data_to_ini(ve::Data* cfg_d, const QString &path) {
    QSettings config(path, QSettings::IniFormat);
    foreach (auto group_d, cfg_d->childrenData()) {
        config.beginGroup(group_d->name());
        // 2 layer
        foreach (auto value_d, group_d->childrenData()) {
            config.setValue(value_d->name(), value_d->get());
        }
        config.endGroup();
    }
}

void setup() {
    setupRescue();
    QObject::connect(ve::d("ve.rescue.record"), &ve::Data::changed, [] (const QVariant &v) {
        static QtOperationRecorder r(nullptr);
        if (v.toBool()) {
            qApp->installEventFilter(&r);
        } else {
            qApp->removeEventFilter(&r);
        }
    });

    auto c_d = data::at("ve.config");

    QObject::connect(c_d, &ve::Data::changed, global(), [=] (const QVariant &v) {
        ve_data_to_ini(c_d, v.toString());
    });

    // env
    for (const QString& env_key : c_d->c("env")->childrenDataNames()) {
        QString env_value = c_d->c("env")->c(env_key)->getString();
        qputenv(env_key.toStdString().c_str(), env_value.toStdString().c_str());

        // if (env_key.compare("QT_SCALE_FACTOR", Qt::CaseInsensitive) == 0) { // legacy scaling
        //     double scale_factor = env_value.toDouble();
        //     if (scale_factor > 1.001) {
        //         QFile conf("qt.conf");
        //         if (conf.open(QIODevice::WriteOnly)) {
        //             conf.write("[Platforms]\nWindowsArguments=fontengine=freetype");
        //             conf.close();
        //         }
        //     } else {
        //         QFile conf("qt.conf");
        //         if (conf.exists()) {
        //             conf.setPermissions(QFile::WriteUser);
        //             conf.remove();
        //         }
        //     }
        // }
    }

    // path
    auto path_root_d = c_d->r("path");
    if (path_root_d->hasCmobj()) {
#if defined(Q_OS_WIN)
        QString env_key = "PATH";
        QString env_separator = ";";
#else
        QString env_key = "LD_LIBRARY_PATH";
        QString env_separator = ":";
#endif
        QString full_env_path = qgetenv(env_key.toStdString().c_str());
        QString current_dir = QDir().absolutePath();
        for (auto path_d : path_root_d->childrenData()) {
            QString env_path = path_d->getString();
            if (env_path.startsWith(".")) env_path.replace(0, 1, current_dir);
            env_path.replace("/", QDir::separator());
            full_env_path.append(env_separator + env_path);
        }
        qputenv(env_key.toStdString().c_str(), full_env_path.toLocal8Bit());
    }

    // attr
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts, c_d->r("attr.ShareOpenGLContexts")->getBool(true));
    QCoreApplication::setAttribute(Qt::AA_Use96Dpi, c_d->r("attr.Use96Dpi")->getBool(QCoreApplication::testAttribute(Qt::AA_Use96Dpi)));

    // log
#ifdef Q_OS_ANDROID
    static QtMessageHandler default_handler = nullptr;
    default_handler = qInstallMessageHandler([] (QtMsgType type, const QMessageLogContext& context, const QString& msg) {
        switch (type) {
             case QtDebugMsg: imol::FLog(LOG_LEVEL_STR_DEBUG, "", LOG_FILE_NAME_PREFIX, LOG_FILE_NAME_SUFFIX) << msg; break;
             case QtInfoMsg: imol::FLog(LOG_LEVEL_STR_INFO, "", LOG_FILE_NAME_PREFIX, LOG_FILE_NAME_SUFFIX) << msg; break;
             case QtWarningMsg: imol::FLog(LOG_LEVEL_STR_WARNING, "", LOG_FILE_NAME_PREFIX, LOG_FILE_NAME_SUFFIX) << msg; break;
             case QtCriticalMsg: imol::FLog(LOG_LEVEL_STR_ERROR, "", LOG_FILE_NAME_PREFIX, LOG_FILE_NAME_SUFFIX) << msg; break;
             case QtFatalMsg: imol::FLog(LOG_LEVEL_STR_SUDO, "", LOG_FILE_NAME_PREFIX, LOG_FILE_NAME_SUFFIX) << msg; break;
        }
        if (default_handler) default_handler(type, context, msg);
    });
#else
    qInstallMessageHandler([] (QtMsgType type, const QMessageLogContext&, const QString& msg) {
        if constexpr (false) {
            switch (type) {
                case QtDebugMsg: veLogD << msg.toLocal8Bit().constData(); break;
                case QtInfoMsg: veLogI << msg.toStdString(); break;
                case QtWarningMsg: veLogW << msg.toStdString(); break;
                case QtCriticalMsg: veLogE << msg.toStdString(); break;
                case QtFatalMsg: veLogS << msg.toStdString(); break;
            }
        } else {
            QString thread_info = QThread::currentThread() == qApp->thread() ? "M" : QString::number(reinterpret_cast<unsigned long long>(QThread::currentThreadId()) & 0xffffffff, 16);
            switch (type) {
                case QtDebugMsg: veLogD << thread_info.toStdString() << ") " << msg.toLocal8Bit().constData(); break;
                case QtInfoMsg: veLogI << thread_info.toStdString() << ") " << msg.toStdString(); break;
                case QtWarningMsg: veLogW << thread_info.toStdString() << ") " << msg.toStdString(); break;
                case QtCriticalMsg: veLogE << thread_info.toStdString() << ") " << msg.toStdString(); break;
                case QtFatalMsg: veLogS << thread_info.toStdString() << ") " << msg.toStdString(); break;
            }
        }
    });
#endif
    QObject::connect(d("ve.log.debug"), &ve::Data::changed, [] (const QVariant &var) { qDebug() << var.toString(); });
    QObject::connect(d("ve.log.info"), &ve::Data::changed,  [] (const QVariant &var) { qInfo() << var.toString(); });
    QObject::connect(d("ve.log.warning"), &ve::Data::changed,  [] (const QVariant &var) { qWarning() << var.toString(); });
    QObject::connect(d("ve.log.error"), &ve::Data::changed,  [] (const QVariant &var) { qCritical() << var.toString(); });

    QObject::connect(ve::d("ve.log.export"), &ve::Data::changed, [] {
        auto files_d = ve::d("ve.log.export.files");
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

void setup(Data *config_d)
{
    auto c_d = d("ve.config");
    if (config_d != c_d) c_d->copyFrom(global(), config_d); // todo
    setup();
}

void setup(const QString& config_path, const QString& format)
{
    auto c_d = d("ve.config");
    c_d->set(global(), config_path);
    if (format.compare("ini", Qt::CaseInsensitive) == 0) {
        ve_data_from_ini(c_d, config_path);
    } else if (format.compare("json", Qt::CaseInsensitive) == 0) {
        c_d->importFromJson(global(), ve::data::Manager::readFromJson(config_path));
    } else if (format.compare("xml", Qt::CaseInsensitive) == 0) {
        ve::data::Manager::readFromXmlFile(global(), c_d, config_path);
    } else if (format.compare("bin", Qt::CaseInsensitive) == 0) {
        c_d->importFromBin(global(), ve::data::Manager::readFromBin(config_path));
    }
    setup();
}

Vector<Module*> g_modules;

void init()
{
    auto c_d = data::at("ve.config");

    // splash screen
    if (c_d->r("entry.splash.enable")->getBool()) {
        auto splash_d = c_d->r("entry.splash");

        QSplashScreen *splash_screen = nullptr;
#define DEFAULT_SPLASH_PIXMAP ":/imol/splash.png"
#define DEFAULT_SPLASH_PIXMAP_HIGH_DPI ":/imol/splash@2x.png"

        QIcon splash_icon;
        QSize splash_size;
        QString splash_path = splash_d->r("pixmap")->getString();
        if (splash_path.isEmpty()) {
            QPixmap p(DEFAULT_SPLASH_PIXMAP);
            splash_icon.addPixmap(p);
            splash_size = p.size();
            double scale_factor = c_d->r("env.qt_scale_factor")->getDouble();
            if (scale_factor > 1.01) splash_icon.addPixmap(QPixmap(DEFAULT_SPLASH_PIXMAP_HIGH_DPI));
        } else {
            QPixmap p(splash_path);
            splash_icon.addPixmap(p);
            splash_size = p.size();
        }
        if (splash_d->hasRmobj("size")) {
            splash_size.setWidth(splash_d->r("size.w")->getInt());
            splash_size.setHeight(splash_d->r("size.h")->getInt());
        }
        splash_screen = new QSplashScreen(splash_icon.pixmap(splash_size));

        //start imol with splash
        splash_screen->setFont(QFont(splash_d->r("font_family")->getString("Microsoft YaHei UI"),
                                     splash_d->r("font_size")->getInt(-1),
                                     splash_d->r("font_weight")->getInt(-1),
                                     splash_d->r("font_italic")->getBool(false)));
        if (!splash_screen->pixmap().isNull()) splash_screen->show();

        splash_d->set(global(), true);
        QObject::connect(splash_d, &ve::Data::changed, splash_screen, [=] {
            splash_screen->finish(QApplication::activeWindow());
            splash_screen->deleteLater();
        });
    }

    // terminal
    if (c_d->r("entry.terminal.enable")->getBool()) {
        terminal::widget()->resize(800, 600);
        terminal::widget()->show();
    }

    // start server
    if (c_d->r("entry.server.enable")->getBool(true)) {
        terminal::startServer(c_d->r("entry.server.port")->getInt(5059), global());
        QApplication::processEvents();
    }

    bool verbose = c_d->r("entry.verbose")->getBool();

    // load plugins
    int module_index = 0;

    for (const auto& it : globalModuleFactory()) {
        QString module_name = QString::fromStdString(it.key);
        if (verbose) qInfo() << "Load module " << module_name;
        Module* m = nullptr;
        try {
            m = globalModuleFactory().produce(it.key);
            g_modules.append(m);
            if (verbose) qInfo() << "Module loaded successfully";
        } catch (std::exception& e) {
            qCritical() << "Load failed: " << e.what();
            continue;
        }
        module_index++;

        ve::d("ve.module." + module_name)->set(global(), QVariant::fromValue(m)); // global module access
    }

    for (int i = 0; i < module_index; i++) {
        Module* m = g_modules[i];
        if (!m) continue;
        if (verbose) qInfo() << "Module " << QString::fromStdString(m->name()) << " init";
        m->exeState<Module::INIT>();
        if (verbose) qInfo() << "Done";
    }

    for (int i = 0; i < module_index; i++) {
        Module* m = g_modules[i];
        if (!m) continue;
        if (verbose) qInfo() << "Module " << QString::fromStdString(m->name()) << " get ready";
        m->exeState<Module::READY>();
        if (verbose) qInfo() << "Done";
    }

    if (verbose) qInfo() << "---------- ve init completed ----------";
}

void deinit()
{
    bool verbose = d("ve.config")->r("entry.verbose")->getBool();

    for (int i = 0; i < g_modules.sizeAsInt(); i++) {
        Module* m = g_modules[i];
        if (!m) continue;
        if (verbose) qInfo() << "Module " << QString::fromStdString(m->name()) << " deinit";
        m->exeState<Module::DEINIT>();
        if (verbose) qInfo() << "Done";
    }

    for (int i = 0; i < g_modules.sizeAsInt(); i++) {
        Module* m = g_modules[i];
        if (!m) continue;
        if (verbose) qInfo() << "Destroy module " << QString::fromStdString(m->name());
        delete m;
        if (verbose) qInfo() << "Done";
    }

    if (ve::terminal::widget()->isActiveWindow()) {
        ve::terminal::widget()->setParent(nullptr);
        ve::terminal::widget()->close();
    }

    if (verbose) qInfo() << "---------- ve deinit completed ----------";
}

}

namespace data {

bool wait(ve::Data* trigger_d, int timeout, bool block_input) {
    QEventLoop el;
    QObject::connect(trigger_d, &ve::Data::changed, &el, &QEventLoop::quit, Qt::DirectConnection);
    QTimer::singleShot(timeout, &el, [&] { el.exit(-1); });
    return el.exec(block_input ? QEventLoop::ExcludeUserInputEvents : QEventLoop::AllEvents) >= 0;
}

}

}
