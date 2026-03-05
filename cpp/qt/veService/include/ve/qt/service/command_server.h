// ----------------------------------------------------------------------------
// command_server.h
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------

#pragma once

#include <veCommon>

namespace ve::server::command {

VE_API void start(Data* d = nullptr);
VE_API void stop();

}
