#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QtWebView>

#include "ve/core/log.h"
#include "ve/entry.h"
#include "ve/qt/qml/qml_register.h"
#include "ve/qt/qt_entry.h"

int main(int argc, char* argv[])
{
    ve::log::setAppName("veQtBrowser");

    std::string configFile = "browser.json";
    if (argc > 1) {
        configFile = argv[1];
    }
    ve::entry::setup(configFile);
    ve::qt::applyEarlySettings();

    QtWebView::initialize();

    QGuiApplication app(argc, argv);
    app.setApplicationName("veQtBrowser");

    ve::entry::init();

    ve::registerQuickNodeQml();

    QQmlApplicationEngine engine;

    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        veLogE << "[veQtBrowser] Failed to load QML";
        ve::entry::deinit();
        return -1;
    }

    veLogI << "[veQtBrowser] running";
    int result = app.exec();

    ve::entry::deinit();
    return result;
}
