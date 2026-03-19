// ----------------------------------------------------------------------------
// ProcessorModule.h - Watches sensor data, computes statistics & alerts
//
//  Data tree (created by this module):
//    processor.temperature.average  double
//    processor.temperature.min      double
//    processor.temperature.max      double
//    processor.humidity.average     double
//    processor.alert                string  "" or warning message
//
//  Observes:
//    sensor.temperature.value  →  accumulates stats, checks thresholds
//    sensor.humidity.value     →  accumulates stats
//    sensor.status             →  resets stats on sensor online
// ----------------------------------------------------------------------------

#pragma once

#include <veCommon>
#include <veModule>

class ProcessorModule : public ve::Module
{
protected:
    void init() override;
    void ready() override;
    void deinit() override;
};
