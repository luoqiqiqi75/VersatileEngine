#pragma once

#include "ve/core/module.h"

class ExampleModule : public ve::Module
{
public:
    explicit ExampleModule(const std::string& name);

protected:
    void init() override;
    void ready() override;
    void deinit() override;
};
