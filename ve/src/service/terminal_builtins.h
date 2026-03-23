// terminal_builtins.h — internal to libve service sources; not a public API.
#pragma once

namespace ve {

// Registers TCP Terminal REPL commands (ls, get, set, ...). Idempotent.
void terminalBuiltinsEnsureRegistered();

} // namespace ve
