// ----------------------------------------------------------------------------
// SensorModule.cpp - Periodic sensor simulation
//
//  Produces temperature & humidity readings once per second.
//  Other modules observe the data tree to react to changes.
// ----------------------------------------------------------------------------

#include "SensorModule.h"
#include <QTimer>
#include <cmath>

VE_REGISTER_MODULE(sensor, SensorModule)

void SensorModule::init()
{
    veLogI << "[Sensor] init - building data tree";

    // -- build the sensor sub-tree --
    ve::d("sensor.status")->set(false);
    ve::d("sensor.sample_count")->set(0);

    ve::d("sensor.temperature.value")->set(20.0);
    ve::d("sensor.temperature.unit")->set(QString::fromUtf8("°C"));

    ve::d("sensor.humidity.value")->set(50.0);
    ve::d("sensor.humidity.unit")->set(QString("%"));
}

void SensorModule::ready()
{
    veLogI << "[Sensor] ready - starting periodic sampling (1 Hz)";

    // go online
    ve::d("sensor.status")->set(true);

    auto* timer = new QTimer(ve::global());
    QObject::connect(timer, &QTimer::timeout, [timer] {
        int n = ve::d("sensor.sample_count")->getInt() + 1;
        ve::d("sensor.sample_count")->set(n);

        // simulate temperature: 22 ± 6 °C  (sine wave + noise)
        double t = 22.0 + 6.0 * std::sin(n * 0.3) + (n % 3 - 1) * 0.5;
        ve::d("sensor.temperature.value")->set(t);

        // simulate humidity: 55 ± 10 %
        double h = 55.0 + 10.0 * std::cos(n * 0.2) + (n % 5 - 2) * 0.3;
        ve::d("sensor.humidity.value")->set(h);

        if (n >= 20) {
            veLogI << "[Sensor] 20 samples produced - stopping";
            ve::d("sensor.status")->set(false);
            timer->stop();
            timer->deleteLater();
        }
    });
    timer->start(1000);
}

void SensorModule::deinit()
{
    veLogIs << "[Sensor] deinit - total samples:"
            << ve::d("sensor.sample_count")->getInt();
}
