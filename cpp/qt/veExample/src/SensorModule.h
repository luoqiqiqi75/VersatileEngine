// ----------------------------------------------------------------------------
// SensorModule.h - Simulates a hardware sensor producing periodic readings
//
//  Data tree (created by this module):
//    sensor.status           bool    online / offline
//    sensor.sample_count     int     total samples produced
//    sensor.temperature.value  double  current temperature (°C)
//    sensor.temperature.unit   string  "°C"
//    sensor.humidity.value     double  current humidity (%)
//    sensor.humidity.unit      string  "%"
// ----------------------------------------------------------------------------

#pragma once

#include <veCommon>
#include <veModule>

class SensorModule : public ve::Module
{
protected:
    void init() override;
    void ready() override;
    void deinit() override;
};
