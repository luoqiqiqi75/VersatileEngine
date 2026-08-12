#pragma once

#include <functional>

namespace ve::platform {

// Watches process termination independently of any event-loop backend.
bool startProcessSignalWatcher(std::function<void()> callback);
void stopProcessSignalWatcher();

} // namespace ve::platform
