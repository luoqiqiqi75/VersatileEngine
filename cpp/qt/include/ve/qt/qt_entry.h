// ----------------------------------------------------------------------------
// qt_entry.h — Qt settings that must run after ve::entry::setup, before QGuiApplication
// ----------------------------------------------------------------------------
#pragma once

#include "ve/global.h"

namespace ve::qt {

/// Reads `ve/entry/module/ve.qt/config` (after JSON setup) and applies Qt application
/// attributes, environment variables, and PATH-style prepends. Call before constructing
/// QGuiApplication / QApplication when AA_* flags matter.
VE_API void applyEarlySettings();

} // namespace ve::qt
