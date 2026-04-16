// ----------------------------------------------------------------------------
// terminal.h
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------

#pragma once

#include "ve/global.h"

class QWidget;
class QObject;

namespace ve {

namespace terminal {

VE_API QWidget* widget();
VE_API void startServer(int port, QObject* parent = nullptr);

}

}
