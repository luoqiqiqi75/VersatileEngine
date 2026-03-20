#include "BrowserModule.h"

#include "ve/core/log.h"
#include "ve/core/node.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QtWebView>

namespace {

std::string resolveRelativePath(std::string_view raw)
{
    if (raw.empty()) {
        return {};
    }
    QString q = QString::fromUtf8(raw.data(), static_cast<int>(raw.size()));
    QFileInfo fi(q);
    if (fi.isAbsolute()) {
        return QDir::cleanPath(fi.absoluteFilePath()).toStdString();
    }
    const QString base = QCoreApplication::applicationDirPath();
    if (base.isEmpty()) {
        return std::string(raw);
    }
    return QDir::cleanPath(QDir(base).absoluteFilePath(q)).toStdString();
}

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

BrowserModule::BrowserModule(const std::string& name) : Module(name) {}

void BrowserModule::init()
{
    QtWebView::initialize();

    std::string staticRoot = node()->find("config/static_root")->getString("");
    if (!staticRoot.empty()) {
        staticRoot = resolveRelativePath(staticRoot);
        auto* httpCfg = ve::n("ve/service/http/config");
        if (httpCfg) {
            httpCfg->at("static_root")->set(ve::Var(staticRoot));
            veLogI << "[browser] static_root -> " << staticRoot;
        }
        node()->at("runtime/resolved_static_root")->set(ve::Var(staticRoot));
    }

    int httpPort = node()->find("config/http_port")->getInt(0);
    if (httpPort > 0) {
        auto* httpCfg = ve::n("ve/service/http/config");
        if (httpCfg) {
            httpCfg->at("port")->set(ve::Var(httpPort));
        }
    }

    int wsPort = node()->find("config/ws_port")->getInt(0);
    if (wsPort > 0) {
        auto* wsCfg = ve::n("ve/service/ws/config");
        if (wsCfg) {
            wsCfg->at("port")->set(ve::Var(wsPort));
        }
    }
}

void BrowserModule::ready()
{
    int hp = node()->find("config/http_port")->getInt(8080);
    if (hp <= 0) {
        hp = 8080;
    }
    if (ve::Node* hr = ve::node::root()->find("ve/service/http/runtime/port")) {
        if (hr->hasValue()) {
            hp = hr->getInt(hp);
        }
    }

    int wp = node()->find("config/ws_port")->getInt(8081);
    if (wp <= 0) {
        wp = 8081;
    }
    if (ve::Node* wr = ve::node::root()->find("ve/service/ws/runtime/port")) {
        if (wr->hasValue()) {
            wp = wr->getInt(wp);
        }
    }

    const std::string home = std::string("http://127.0.0.1:") + std::to_string(hp) + "/";
    node()->at("runtime/home_url")->set(ve::Var(home));
    veLogI << "[browser] ready  home_url=" << home;

    // Read veservice.js from QRC, bake in WS URL, store in node tree for QML injection
    QString jsSrc = readQrcFile(QStringLiteral(":/js/veservice.js"));
    if (!jsSrc.isEmpty()) {
        QString preamble = QStringLiteral("var _ve_ws_url = \"ws://127.0.0.1:%1\";\n").arg(wp);
        std::string fullJs = (preamble + jsSrc).toStdString();
        ve::n("browser/javascript/global")->set(ve::Var(fullJs));
        veLogI << "[browser] veservice.js stored (" << fullJs.size() << " bytes, ws_port=" << wp << ")";
    }

    ve::n("browser/url")->set(ve::Var(home));
    ve::n("browser/refresh")->set(0);

    bool showToolbar = false;
    if (auto* tb = node()->find("config/toolbar")) {
        showToolbar = tb->getBool(false);
    }
    ve::n("browser/toolbar/visible")->set(ve::Var(showToolbar));
}

void BrowserModule::deinit()
{
    veLogI << "[browser] deinit";
}
