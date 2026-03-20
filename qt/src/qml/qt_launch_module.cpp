#include "ve/core/log.h"
#include "ve/core/loop.h"
#include "ve/core/module.h"
#include "ve/core/node.h"

#include <QQmlApplicationEngine>
#include <QUrl>

namespace ve::qt {

namespace {

class QtLaunchModule : public Module
{
    QQmlApplicationEngine* engine_ = nullptr;

public:
    explicit QtLaunchModule(const std::string& name) : Module(name) {}

protected:
    void ready() override
    {
        Node* cfg = node()->find("config");
        if (!cfg) {
            return;
        }

        std::string launch_type = "qml";
        if (Node* tn = cfg->find("type")) {
            launch_type = tn->getString("qml");
        }

        if (launch_type == "qml") {
            launchQml(cfg);
        }
    }

    void deinit() override
    {
        delete engine_;
        engine_ = nullptr;
    }

private:
    void launchQml(Node* cfg)
    {
        std::string main_qml = "qrc:/qml/Main.qml";
        if (Node* mn = cfg->find("main_qml")) {
            main_qml = mn->getString(main_qml);
        }

        engine_ = new QQmlApplicationEngine();
        engine_->load(QUrl(QString::fromStdString(main_qml)));

        if (engine_->rootObjects().isEmpty()) {
            veLogE << "[ve.qt.launch] Failed to load QML: " << main_qml;
            loop::quit(-1);
            return;
        }

        veLogI << "[ve.qt.launch] QML loaded: " << main_qml;
    }
};

} // namespace

} // namespace ve::qt

VE_REGISTER_PRIORITY_MODULE(ve.qt.launch, ve::qt::QtLaunchModule, 6, 1)

