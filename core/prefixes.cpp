#include "prefixes.h"

#include "config.h"
#include "log.h"

#include <cstring>

namespace mtags {

namespace {

// One entry from a "|"-separated prefix list. "*" is a wildcard entry the
// original handled with its own "Setting %s prefix: *" trace.
bool entry_matches(const char* entry, size_t len, const std::string& path) {
    if (len == 1 && entry[0] == '*') {
        return true;
    }
    return len != 0 && path.size() >= len && std::memcmp(path.data(), entry, len) == 0;
}

bool matches_any(const char* list, const std::string& path) {
    for (const char* p = list; p != nullptr && *p != '\0';) {
        const char* bar = std::strchr(p, '|');
        const size_t len = bar != nullptr ? static_cast<size_t>(bar - p) : std::strlen(p);
        if (entry_matches(p, len, path)) {
            return true;
        }
        if (bar == nullptr) {
            break;
        }
        p = bar + 1;
    }
    return false;
}

// The original emptied and re-logged each list once, which is when it read the
// settings, and it ran the same code for all three lists - passing the name, which
// is why its strings are templates. `dump` is the ungated line the two configured
// lists have (the streaming list has none).
void trace_prefixes(const char* name, const char* list, const char* dump, bool& done) {
    if (done) {
        return;
    }
    done = true;

    log_verbose("Clearing %s prefixes", name);
    for (const char* p = list; p != nullptr && *p != '\0';) {
        const char* bar = std::strchr(p, '|');
        const size_t len = bar != nullptr ? static_cast<size_t>(bar - p) : std::strlen(p);
        if (len == 1 && p[0] == '*') {
            log_verbose("Setting %s prefix: *", name);
        }
        else {
            log_verbose("Setting %s prefix: %s", name, std::string(p, len).c_str());
        }
        if (bar == nullptr) {
            break;
        }
        p = bar + 1;
    }
    if (dump != nullptr) {
        log_error(dump, list);
    }
}

// A stream is never local, whatever the prefix lists say. The original drove this
// from its own ".stream" / "/stream/" markers, set up like the other two lists and
// traced under the name "streaming".
const char* const streaming_prefixes = ".stream|/stream/";

bool looks_streaming(const std::string& path) {
    return path.find("/stream/") != std::string::npos
        || (path.size() >= 7 && path.compare(path.size() - 7, 7, ".stream") == 0);
}

} // namespace

t_media classify_media(const std::string& path) {
    static bool local_read = false;
    static bool remote_read = false;
    static bool streaming_read = false;

    const pfc::string8 local = config::local_prefixes();
    trace_prefixes("local", local, "Local prefixes: %s", local_read);

    const pfc::string8 remote = config::remote_prefixes();
    // The remote list was traced only when it had entries - an empty list produced
    // no output at all.
    if (!remote.is_empty()) {
        trace_prefixes("remote", remote, "Remote prefixes: %s", remote_read);
    }

    // The streaming list is only read when the path actually looks like a stream: a
    // normal read of the original's console shows the local list and nothing else,
    // and none of its probes produced a streaming line.
    if (looks_streaming(path)) {
        trace_prefixes("streaming", streaming_prefixes, nullptr, streaming_read);
        return t_media::remote;
    }

    if (matches_any(local, path)) {
        return t_media::local;
    }
    return t_media::remote;
}

const char* media_name(t_media m) {
    return m == t_media::local ? "local" : "remote";
}

} // namespace mtags
