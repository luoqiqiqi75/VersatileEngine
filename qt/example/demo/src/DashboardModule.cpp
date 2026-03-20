// ----------------------------------------------------------------------------
// DashboardModule.cpp - Cross-module data aggregation
//
//  Demonstrates:
//    - Reading data nodes created by other modules
//    - Reacting to signals from processor.alert (triggered by ProcessorModule)
//    - Building a composite summary from the entire data tree
//    - Module ordering: DashboardModule::ready() runs after Sensor & Processor
//      have already set up their observers, so data flows correctly.
//
//  Data flow:
//    SensorModule ──► ProcessorModule ──► DashboardModule
//        (produce)       (analyze)          (display)
// ----------------------------------------------------------------------------

#include "DashboardModule.h"

VE_REGISTER_MODULE(dashboard, DashboardModule)

namespace {

void refreshSummary()
{
    int n = imol::legacy::d(QStringLiteral("dashboard.refresh_count"))->getInt() + 1;
    imol::legacy::d(QStringLiteral("dashboard.refresh_count"))->set(n);

    QString report;
    report += QString("=== Dashboard Report #%1 ===\n").arg(n);
    report += QString("  Sensor online  : %1\n")
                  .arg(imol::legacy::d(QStringLiteral("sensor.status"))->get().toBool() ? "YES" : "NO");
    report += QString("  Samples        : %1\n")
                  .arg(imol::legacy::d(QStringLiteral("sensor.sample_count"))->getInt());
    report += QString("  Temperature    : %1 %2  (avg %3, min %4, max %5)\n")
                  .arg(imol::legacy::d(QStringLiteral("sensor.temperature.value"))->getDouble(), 0, 'f', 1)
                  .arg(imol::legacy::d(QStringLiteral("sensor.temperature.unit"))->getString())
                  .arg(imol::legacy::d(QStringLiteral("processor.temperature.average"))->getDouble(), 0, 'f', 2)
                  .arg(imol::legacy::d(QStringLiteral("processor.temperature.min"))->getDouble(), 0, 'f', 2)
                  .arg(imol::legacy::d(QStringLiteral("processor.temperature.max"))->getDouble(), 0, 'f', 2);
    report += QString("  Humidity       : %1 %2  (avg %3)\n")
                  .arg(imol::legacy::d(QStringLiteral("sensor.humidity.value"))->getDouble(), 0, 'f', 1)
                  .arg(imol::legacy::d(QStringLiteral("sensor.humidity.unit"))->getString())
                  .arg(imol::legacy::d(QStringLiteral("processor.humidity.average"))->getDouble(), 0, 'f', 2);

    QString alert = imol::legacy::d(QStringLiteral("processor.alert"))->getString();
    if (!alert.isEmpty())
        report += QString("  *** ALERT: %1\n").arg(alert);

    imol::legacy::d(QStringLiteral("dashboard.summary"))->set(report);

    veLogI << "[Dashboard] report #" << n;
    veLogI << report.toStdString();
}

} // anonymous namespace

void DashboardModule::init()
{
    veLogI << "[Dashboard] init - creating dashboard nodes";

    imol::legacy::d(QStringLiteral("dashboard.refresh_count"))->set(0);
    imol::legacy::d(QStringLiteral("dashboard.summary"))->set(QString("(no data yet)"));
}

void DashboardModule::ready()
{
    veLogI << "[Dashboard] ready - subscribing to cross-module events";

    // -- React to alerts from ProcessorModule --
    imol::legacy::on(QStringLiteral("processor.alert"), ve::global(), [](const QVariant& v, const QVariant&, QObject*) {
        QString alert = v.toString();
        if (!alert.isEmpty()) {
            veLogW << "[Dashboard] *** ALERT *** " << alert.toStdString();
        }
    });

    imol::legacy::on(QStringLiteral("sensor.sample_count"), ve::global(), [](const QVariant& v, const QVariant&, QObject*) {
        if (v.toInt() % 5 == 0 && v.toInt() > 0) {
            refreshSummary();
        }
    });

    imol::legacy::on(QStringLiteral("sensor.status"), ve::global(), [](const QVariant& v, const QVariant&, QObject*) {
        if (!v.toBool()) {
            veLogI << "[Dashboard] sensor offline - generating final report";
            refreshSummary();
        }
    });
}

void DashboardModule::deinit()
{
    veLogIs << "[Dashboard] deinit - generated"
            << imol::legacy::d(QStringLiteral("dashboard.refresh_count"))->getInt() << "reports total";
}
