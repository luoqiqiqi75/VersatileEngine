// ----------------------------------------------------------------------------
// ProcessorModule.cpp — Real-time statistics from sensor data
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
    veLogI << "[Processor] init — creating statistics nodes";

    ve::d("processor.temperature.average")->set(0.0);
    ve::d("processor.temperature.min")->set(0.0);
    ve::d("processor.temperature.max")->set(0.0);
    ve::d("processor.humidity.average")->set(0.0);
    ve::d("processor.alert")->set(QString(""));

    g_temp.reset();
    g_humi.reset();
}

void ProcessorModule::ready()
{
    veLogI << "[Processor] ready — subscribing to sensor data";

    // -- React to sensor status changes --
    ve::data::on("sensor.status", ve::global(), [] (const QVariant& v) {
        if (v.toBool()) {
            veLogI << "[Processor] sensor came online — resetting accumulators";
            g_temp.reset();
            g_humi.reset();
        } else {
            veLogIs << "[Processor] sensor went offline — final avg temp:"
                    << QString::number(g_temp.average(), 'f', 2).toStdString();
        }
    });

    // -- React to temperature changes --
    ve::data::on("sensor.temperature.value", ve::global(), [] (const QVariant& v) {
        double t = v.toDouble();
        g_temp.push(t);

        ve::d("processor.temperature.average")->set(g_temp.average());
        ve::d("processor.temperature.min")->set(g_temp.min);
        ve::d("processor.temperature.max")->set(g_temp.max);

        // threshold alert — writes to processor.alert, which DashboardModule observes
        if (t > TEMP_ALERT_HIGH) {
            QString msg = QString("HIGH temperature: %1 °C (threshold %2)")
                              .arg(t, 0, 'f', 1).arg(TEMP_ALERT_HIGH, 0, 'f', 1);
            ve::d("processor.alert")->set(msg);
        } else if (t < TEMP_ALERT_LOW) {
            QString msg = QString("LOW temperature: %1 °C (threshold %2)")
                              .arg(t, 0, 'f', 1).arg(TEMP_ALERT_LOW, 0, 'f', 1);
            ve::d("processor.alert")->set(msg);
        } else {
            ve::d("processor.alert")->set(QString(""));
        }
    });

    // -- React to humidity changes --
    ve::data::on("sensor.humidity.value", ve::global(), [] (const QVariant& v) {
        g_humi.push(v.toDouble());
        ve::d("processor.humidity.average")->set(g_humi.average());
    });
}

void ProcessorModule::deinit()
{
    veLogIs << "[Processor] deinit — processed"
            << g_temp.count << "temperature samples,"
            << g_humi.count << "humidity samples";
}
