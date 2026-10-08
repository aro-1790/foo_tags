#pragma once

// The small text helpers every module needs, in one place: all pfc's own - the
// case maps are Unicode-aware (upper through the same uCharUpper the original
// called per character), the comparisons are the primitives the original used,
// and the extension walk knows about path separators.

#include <SDK/foobar2000-lite.h>

#include <cstring>
#include <string>

namespace mtags {

inline std::string lowercase(const char* s) {
    return pfc::stringToLower(s).c_str();
}

inline std::string lowercase(std::string s) {
    return lowercase(s.c_str());
}

inline std::string uppercase(const char* s) {
    return pfc::stringToUpper(s).c_str();
}

inline std::string uppercase(std::string s) {
    return uppercase(s.c_str());
}

// Case-sensitive and case-insensitive "does s start with this": pfc's partial
// compares, which are what the original used to test names and extensions.
inline bool starts_with(const std::string& s, const char* prefix) {
    return pfc::strcmp_partial(s.c_str(), prefix) == 0;
}

inline bool starts_with_ci(const char* s, const char* prefix) {
    return pfc::stricmp_ascii_partial(s, prefix) == 0;
}

inline bool starts_with_ci(const char* s, const std::string& prefix) {
    return starts_with_ci(s, prefix.c_str());
}

inline bool ends_with_ci(const std::string& s, const char* tail) {
    const size_t n = std::strlen(tail);
    return s.size() >= n && pfc::stricmp_ascii_partial(s.c_str() + (s.size() - n), tail) == 0;
}

inline bool ends_with_ci(const std::string& s, const std::string& tail) {
    return ends_with_ci(s, tail.c_str());
}

// The extension without its dot, empty when there is none.
inline std::string extension_of(const std::string& name) {
    return pfc::string_extension(name.c_str()).c_str();
}

} // namespace mtags
