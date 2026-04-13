#include "BrowserModule.h"

#include "ve/core/log.h"
#include "ve/core/node.h"

#include <QFile>
#include <QString>
#include <QtWebView>

namespace {

QString readQrcFile(const QString& qrcPath)
{
    QFile f(qrcPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(f.readAll());
}

} // namespace

VE_REGISTER_MODULE(browser, BrowserModule)

BrowserModule::BrowserModule(const std::string& name) : Module(name)
{
    QtWebView::initialize();
}

void BrowserModule::ready()
{
    // 读取 StaticServer 实际运行端口（默认 12400）
    int hp = ve::n("ve/server/static/runtime/port")->getInt(12400);
    // 读取 NodeWsServer 实际运行端口（默认 12100）
    int wp = ve::n("ve/server/node/ws/runtime/port")->getInt(12100);

    const std::string home = std::string("http://127.0.0.1:") + std::to_string(hp) + "/";
    node()->at("runtime/home_url")->set(ve::Var(home));
    veLogI << "[browser] ready  home_url=" << home;

    QString jsSrc = readQrcFile(QStringLiteral(":/js/veservice.js"));
    if (!jsSrc.isEmpty()) {
        QString preamble = QStringLiteral("var _ve_ws_url = \"ws://127.0.0.1:%1\";\n").arg(wp);
        std::string fullJs = (preamble + jsSrc).toStdString();
        ve::n("browser/javascript/global")->set(ve::Var(fullJs));
        veLogI << "[browser] veservice.js stored (" << fullJs.size() << " bytes, ws_port=" << wp << ")";
    }

    ve::n("browser/url")->set(ve::Var(home));
    ve::n("browser/refresh")->set(0);
}

void BrowserModule::deinit()
{
    veLogI << "[browser] deinit";
}
