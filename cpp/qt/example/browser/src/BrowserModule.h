#pragma once

#include "ve/core/module.h"

class BrowserModule : public ve::Module
{
public:
    explicit BrowserModule(const std::string& name);

protected:
    void init() override;
    void ready() override;
    void deinit() override;
};
