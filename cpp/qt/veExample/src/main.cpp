// ----------------------------------------------------------------------------
// veExample — main.cpp
//
//  Multi-module demo showing reactive data flow across VE modules.
//
//  Registered modules (via VE_REGISTER_MODULE):
//    sensor      — Simulates temperature & humidity readings (1 Hz)
//    processor   — Computes min/max/avg, raises threshold alerts
//    dashboard   — Aggregates cross-module data into periodic reports
//
//  Data flow:
//    sensor.temperature.value ──► processor.temperature.{avg,min,max}
//    sensor.humidity.value    ──► processor.humidity.average
//    processor.alert          ──► dashboard (warning log)
//    sensor.sample_count      ──► dashboard (periodic summary every 5 samples)
//    sensor.status            ──► dashboard (final report on offline)
//
//  Open the VE Terminal to browse the live data tree while it runs.
// ----------------------------------------------------------------------------

#include <QApplication>
#include <QWidget>

#include <veCommon>
#include "ve/qt/widget/terminal.h"

int main(int argc, char* argv[])
{
    ve::log::setAppName("veExample");

    ve::log::line<>();
    veLogI << "VersatileEngine — Multi-Module Example";
    ve::log::line<>();

    // 1. Setup — load config, create root data tree
    ve::entry::setup("config.ini");

    QApplication app(argc, argv);

    // 2. Init — instantiate & init all registered modules
    //    Order: sensor → processor → dashboard (alphabetical by key)
    ve::entry::init();

    // 3. Show the terminal widget for live data-tree inspection
    if (auto* tw = ve::terminal::widget()) {
        tw->setWindowTitle("VersatileEngine Terminal");
        tw->resize(880, 560);
        tw->show();
    }

    veLogI << "Application running — watch the terminal for live updates.";
    int result = app.exec();

    // 4. Deinit — tear down all modules in reverse order
    ve::entry::deinit();

    veLogIs << "Application exited with code" << result;
    return result;
}
