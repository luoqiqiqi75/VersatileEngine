// ----------------------------------------------------------------------------
// veExample - main.cpp
//
//  Multi-module demo showing reactive data flow across VE modules.
// ----------------------------------------------------------------------------

#include <QApplication>

#include "ve/core/log.h"
#include "ve/entry.h"
#include "ve/qt/qt_entry.h"
#include "ve/qt/widget/terminal.h"

int main(int argc, char* argv[])
{
    ve::log::setAppName("veExample");

    ve::log::line<>();
    veLogI << "VersatileEngine - Multi-Module Example";
    ve::log::line<>();

    std::string configFile = "example.json";
    if (argc > 1) {
        configFile = argv[1];
    }
    ve::entry::setup(configFile);
    ve::qt::applyEarlySettings();

    QApplication app(argc, argv);

    ve::entry::init();

    if (auto* tw = ve::terminal::widget()) {
        tw->setWindowTitle("VersatileEngine Terminal");
        tw->resize(880, 560);
        tw->show();
    }

    veLogI << "Application running - watch the terminal for live updates.";
    int result = app.exec();

    ve::entry::deinit();

    veLogIs << "Application exited with code" << result;
    return result;
}
