#include "log.h"

#include "config.h"

#include <cstdarg>
#include <cstdio>
#include <string>

namespace mtags {

namespace {

// printf-style formatting without pulling in pfc's variadic helpers, so a
// message longer than the stack buffer still comes out whole.
std::string format_message(const char* fmt, va_list args) {
    char buffer[512];

    va_list probe;
    va_copy(probe, args);
    const int needed = std::vsnprintf(buffer, sizeof(buffer), fmt, probe);
    va_end(probe);

    if (needed < 0) {
        return std::string();
    }
    if (static_cast<size_t>(needed) < sizeof(buffer)) {
        return std::string(buffer, static_cast<size_t>(needed));
    }

    std::string big(static_cast<size_t>(needed), '\0');
    std::vsnprintf(&big[0], big.size() + 1, fmt, args);
    return big;
}

} // namespace

void log_verbose(const char* fmt, ...) {
    if (!config::verbose_logging()) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    const std::string message = format_message(fmt, args);
    va_end(args);

    console::printf("%s%s", "m-TAGS:: ", message.c_str());
}

void log_error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const std::string message = format_message(fmt, args);
    va_end(args);

    // The ungated logger carries the prefix too: a console log from the original
    // with tracing off shows "m-TAGS:: Local prefixes: file:|unpack:|cdda:".
    console::printf("%s%s", "m-TAGS:: ", message.c_str());
}

} // namespace mtags
