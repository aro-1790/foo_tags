#pragma once

// Classifying the media a locator resolves to.
//
// Advanced Preferences > Tagging > m-TAGS > Media prefixes holds two
// "|"-separated lists. A resolved path that begins with one of the Local entries
// is "local"; everything else is "remote". The original logged the answer for
// every resolve ("Media is %s"), and parsed the lists only once per process -
// which is why only the first resolve in a session emits the prefix trace.

#include <string>

namespace mtags {

enum class t_media { local, remote };

// The path should already be the canonical one. Returns the classification the
// original logged, and emits that trace on the first call.
t_media classify_media(const std::string& path);

// "local" / "remote", for the "Media is %s" trace.
const char* media_name(t_media m);

} // namespace mtags
