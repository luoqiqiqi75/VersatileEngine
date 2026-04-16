// qml_register.h — register QuickNode / QuickRootNode for QML (VEQuickNode module)
#pragma once

#include "ve_qml_global.h"

class QQmlEngine;

namespace ve {

/// Call once per process before loading QML that imports VEQuickNode.
VE_QML_API void registerQuickNodeQml();

/// Optional: register on a specific engine (same types; use for tests).
VE_QML_API void registerQuickNodeQml(QQmlEngine* engine);

} // namespace ve
