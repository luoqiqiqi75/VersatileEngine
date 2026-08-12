#include "src/process_signal.h"

namespace ve::platform {

bool startProcessSignalWatcher(std::function<void()>) { return false; }
void stopProcessSignalWatcher() {}

} // namespace ve::platform
