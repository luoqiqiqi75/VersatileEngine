// ----------------------------------------------------------------------------
// DashboardModule.h — Aggregates data from other modules into a summary
//
//  Data tree (created by this module):
//    dashboard.refresh_count   int     how many reports generated
//    dashboard.summary         string  formatted multi-line report
//
//  Observes:
//    processor.alert           →  logs warnings immediately
//    sensor.status             →  triggers a summary refresh on offline
//    sensor.sample_count       →  triggers periodic summary every N samples
// ----------------------------------------------------------------------------

#pragma once

#include <veCommon>
#include <veModule>

class DashboardModule : public ve::Module
{
protected:
    void init() override;
    void ready() override;
    void deinit() override;
};
