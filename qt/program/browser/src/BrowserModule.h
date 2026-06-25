#pragma once

#include "ve/core/module.h"

class BrowserModule : public ve::Module
{
public:
    BrowserModule();

protected:
    void ready() override;
    void deinit() override;
};
