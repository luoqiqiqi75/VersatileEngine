#include "ve/core/common.h"

#include <QCoreApplication>

VE_REGISTER_VERSION("ve.core", 1)

#include "imol/program/config.h"

#include <QDir>
#include <QSplashScreen>
#include <QPixmap>
#include <QIcon>
#include <QApplication>

#include "global_object.h"

#include "imol/program/server.h"
#ifdef WIN32
#include "util/StackWalker/BaseException.h"
#endif

namespace ve {

QObject* global()
{
    static GlobalObject g;
    return &g;
}

namespace version {

int calc_sum(Data* key_d)
{
    int v = number(key_d->fullName(manager().keyMobj()));
    foreach (auto sub_key_d, key_d->cmobjs()) {
        v += calc_sum(sub_key_d);
    }
    return v;
}

Manager& manager() { static Manager m; return m; }

int number(const QString& key, bool sum)
{
    return sum ? calc_sum(manager().keyMobj()->r(key)) : manager().create(key);
}

QString releaseString(const QString &key)
{
    int baseline = manager().create(QString("@0_%1").arg(key));
    int major = manager().create(QString("@1_%1").arg(key));
    int minor = manager().create(QString("@2_%1").arg(key));
    QStringList ver_str_list;
    for (int i = 3; i < 10; i++) {
        QString sub_ver_key = QString("@%1_%2").arg(i).arg(key);
        if (!manager().keyMobj(sub_ver_key)->isEmptyMobj()) ver_str_list.append(QString::number(manager().create(sub_ver_key)));
    }

    QString ver_str = QString("v%1.%2.%3").arg(major).arg(minor).arg(number(key, true) - baseline);
    if (ver_str_list.size() > 0) ver_str.append(".").append(ver_str_list.join("."));
    return ver_str;
}

}

namespace entry {

void setup(const QString &cfg_path)
{
#ifdef WIN32
    SET_DEFAULT_HANDLER();
#endif

    Config c;
    auto c_d = data::at("imol.cfg");

    if (cfg_path.endsWith("ini")) {
        c.loadIni(cfg_path);
    } else if (cfg_path.endsWith("json")) {
        c_d->importFromJson(global(), imol::m().readFromJson(cfg_path));
    }

    c_d->set(global(), cfg_path);
    GlobalObject::connect(c_d, &ve::Data::changed, global(), [=] {
        Config tmp_c;
        tmp_c.saveIni(c_d->getString());
    });

    // scaling
    double scale_factor = c_d->r("env.qt_scale_factor")->getDouble();
    if (scale_factor > 0) {
        qputenv("QT_SCALE_FACTOR", c_d->r("env.qt_scale_factor")->getString().toStdString().c_str());
    }
    if (scale_factor > 1.001) {
        QFile conf("qt.conf");
        if (conf.open(QIODevice::WriteOnly)) {
            conf.write("[Platforms]\nWindowsArguments=fontengine=freetype");
            conf.close();
        }
    } else {
        QFile conf("qt.conf");
        if (conf.exists()) {
            conf.setPermissions(QFile::WriteUser);
            conf.remove();
        }
    }

    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts, c_d->r("attr.share_opengl_contexts")->getBool(true));
    bool attr_eanble_scaling = c_d->r("attr.enable_high_dpi_scaling")->getBool(false);
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling, attr_eanble_scaling);
    QCoreApplication::setAttribute(Qt::AA_DisableHighDpiScaling, !attr_eanble_scaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, c_d->r("attr.use_high_dpi_pixmaps")->getBool(false));

    // path
    auto env_path_d = c_d->r("env.path");
    if (env_path_d->hasCmobj()) {
#if defined(Q_OS_WIN)
        QString env_key = "PATH";
        QString env_separator = ";";
#elif defined(Q_OS_LINUX)
        QString env_key = "LD_LIBRARY_PATH";
        QString env_separator = ":";
#endif
        QString full_env_path = qgetenv(env_key.toStdString().c_str());
        QString app_dir = QCoreApplication::applicationDirPath();
        foreach (auto path_d, env_path_d->cmobjs()) {
            QString env_path = path_d->getString();
            if (env_path.startsWith(".")) env_path.replace(0, 1, app_dir);
            env_path.replace("/", QDir::separator());
            full_env_path.append(env_separator + env_path);
        }
        qputenv(env_key.toStdString().c_str(), full_env_path.toLocal8Bit());
    }
}

void init()
{
    auto c_d = data::at("imol.cfg");

    // splash screen
    if (c_d->r("splash.enable")->getBool()) {
        QSplashScreen *splash_screen = nullptr;
#define DEFAULT_SPLASH_PIXMAP ":/imol/splash.png"
#define DEFAULT_SPLASH_PIXMAP_HIGH_DPI ":/imol/splash@2x.png"

        QIcon splash_icon;
        QSize splash_size;
        QString splash_path = c_d->r("splash.pixmap")->getString();
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
        if (c_d->hasRmobj("splash.size")) {
            splash_size.setWidth(c_d->r("splash.size.w")->getInt());
            splash_size.setHeight(c_d->r("splash.size.h")->getInt());
        }
        splash_screen = new QSplashScreen(splash_icon.pixmap(splash_size));

        //start imol with splash
        splash_screen->setFont(QFont(c_d->r("splash.font_family")->getString("Microsoft YaHei UI"),
                                     c_d->r("splash.font_size")->getInt(-1),
                                     c_d->r("splash.font_weight")->getInt(-1),
                                     c_d->r("splash.font_italic")->getBool(false)));
        if (!splash_screen->pixmap().isNull()) splash_screen->show();

        auto cs_d = c_d->c("splash");
        cs_d->set(global(), true);
        GlobalObject::connect(cs_d, &ve::Data::changed, splash_screen, [=] {
            splash_screen->finish(QApplication::activeWindow());
            splash_screen->deleteLater();
        });
    }

    // terminal
    if (c_d->r("terminal.enable")->getBool()) {
        terminal::widget()->resize(800, 600);
        terminal::widget()->show();
    }

    // start server
    if (c_d->r("server.enable")->getBool(true)) {
        auto server = new Server(global());
        server->start(c_d->r("server.port")->getInt(5059));
        QApplication::processEvents();
    }
}

void deinit()
{

}

}

}
