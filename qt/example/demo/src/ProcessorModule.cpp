// ----------------------------------------------------------------------------
// ProcessorModule.cpp - Real-time statistics from sensor data
//
//  Demonstrates cross-module reactive data flow:
//    sensor.temperature.value  ──(changed)──►  processor computes min/max/avg
//                                              └──► writes processor.alert
// ----------------------------------------------------------------------------

#include "ProcessorModule.h"
#include <algorithm>
#include <cmath>
#include <limits>

VE_REGISTER_MODULE(processor, ProcessorModule)

// -- internal accumulators (module-local) --
namespace {
struct Stats {
    int    count = 0;
    double sum   = 0.0;
    double min   =  std::numeric_limits<double>::max();
    double max   = -std::numeric_limits<double>::max();

    void reset() { count = 0; sum = 0.0; min = std::numeric_limits<double>::max(); max = -std::numeric_limits<double>::max(); }
    void push(double v) { ++count; sum += v; min = std::min(min, v); max = std::max(max, v); }
    double average() const { return count > 0 ? sum / count : 0.0; }
};

Stats g_temp;
Stats g_humi;

constexpr double TEMP_ALERT_HIGH = 26.0;
constexpr double TEMP_ALERT_LOW  = 16.0;
}

void ProcessorModule::init()
{
    veLogI << "[Processor] init - creating statistics nodes";

    imol::legacy::d(QStringLiteral("processor.temperature.average"))->set(0.0);
    imol::legacy::d(QStringLiteral("processor.temperature.min"))->set(0.0);
    imol::legacy::d(QStringLiteral("processor.temperature.max"))->set(0.0);
    imol::legacy::d(QStringLiteral("processor.humidity.average"))->set(0.0);
    imol::legacy::d(QStringLiteral("processor.alert"))->set(QString(""));

    g_temp.reset();
    g_humi.reset();
}

void ProcessorModule::ready()
{
    veLogI << "[Processor] ready - subscribing to sensor data";

    // -- React to sensor status changes --
    imol::legacy::on(QStringLiteral("sensor.status"), ve::global(), [](const QVariant& v, const QVariant&, QObject*) {
        if (v.toBool()) {
            veLogI << "[Processor] sensor came online - resetting accumulators";
            g_temp.reset();
            g_humi.reset();
        } else {
            veLogIs << "[Processor] sensor went offline - final avg temp:"
                    << QString::number(g_temp.average(), 'f', 2).toStdString();
        }
    });

    imol::legacy::on(QStringLiteral("sensor.temperature.value"), ve::global(), [](const QVariant& v, const QVariant&, QObject*) {
        double t = v.toDouble();
        g_temp.push(t);

        imol::legacy::d(QStringLiteral("processor.temperature.average"))->set(g_temp.average());
        imol::legacy::d(QStringLiteral("processor.temperature.min"))->set(g_temp.min);
        imol::legacy::d(QStringLiteral("processor.temperature.max"))->set(g_temp.max);

        // threshold alert - writes to processor.alert, which DashboardModule observes
        if (t > TEMP_ALERT_HIGH) {
            QString msg = QString("HIGH temperature: %1 °C (threshold %2)")
                              .arg(t, 0, 'f', 1).arg(TEMP_ALERT_HIGH, 0, 'f', 1);
            imol::legacy::d(QStringLiteral("processor.alert"))->set(msg);
        } else if (t < TEMP_ALERT_LOW) {
            QString msg = QString("LOW temperature: %1 °C (threshold %2)")
                              .arg(t, 0, 'f', 1).arg(TEMP_ALERT_LOW, 0, 'f', 1);
            imol::legacy::d(QStringLiteral("processor.alert"))->set(msg);
        } else {
            imol::legacy::d(QStringLiteral("processor.alert"))->set(QString(""));
        }
    });

    imol::legacy::on(QStringLiteral("sensor.humidity.value"), ve::global(), [](const QVariant& v, const QVariant&, QObject*) {
        g_humi.push(v.toDouble());
        imol::legacy::d(QStringLiteral("processor.humidity.average"))->set(g_humi.average());
    });
}

void ProcessorModule::deinit()
{
    veLogIs << "[Processor] deinit - processed"
            << g_temp.count << "temperature samples,"
            << g_humi.count << "humidity samples";
}
