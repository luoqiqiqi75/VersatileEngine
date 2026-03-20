// ----------------------------------------------------------------------------
// xservice_server.h
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------

#pragma once

#include "imol/modulemanager.h"
#include "ve/core/module.h"

namespace ve::service {

class VE_API XServiceServer : public Module
{
public:
    XServiceServer();
    ~XServiceServer();

private:
    void init() override;
    void ready() override;
    void deinit() override;

private:
    VE_DECLARE_PRIVATE
};

} // namespace ve::service
