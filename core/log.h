#pragma once

// Console tracing, in the shape the original used.
//
// Every trace line goes through the "m-TAGS:: " prefix, and the verbose ones are
// suppressed unless Advanced Preferences > Tagging > m-TAGS > Enable verbose
// logging is on. That gate is why the decompile's log helper (FUN_100264e0) tests
// the DAT_100820F0 flag before it writes anything.

#include <SDK/foobar2000.h>

namespace mtags {

// Verbose trace. A no-op unless verbose logging is enabled, so callers do not
// have to check first.
void log_verbose(const char* fmt, ...);

// Diagnostics that are shown regardless - the original's plain console messages
// such as "ERROR: m-TAGS is not a JSON Array", which a user needs to see even
// with tracing off.
void log_error(const char* fmt, ...);

} // namespace mtags
