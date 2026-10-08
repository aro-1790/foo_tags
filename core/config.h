#pragma once

// Advanced Preferences tree, matching the original plugin's entries.
//
// The original exposed no Preferences *page* - everything it lets you tune lives
// under Advanced Preferences, beneath a single "m-TAGS" branch.

#include <SDK/foobar2000.h>

namespace mtags {
namespace config {

// Direct children of the "m-TAGS" branch.
bool verbose_logging();
bool do_not_encapsulate_album_art();
bool always_write_all_tags();
bool do_not_overwrite_duration();

// "m-TAGS creator" branch - how .tags files are produced from selected sources.
pfc::string8 folder_wide_name();     // default "!"
bool ignore_source_tags();
bool do_not_parse_archives();
pfc::string8 excluded_extensions();  // default ""
bool keep_source_extensions();
bool overwrite_existing();
bool separate_file_per_source();
bool only_absolute_paths();

// "Media prefixes" branch - locator prefixes treated as local resp. remote.
pfc::string8 local_prefixes();       // default "file:|unpack:|cdda:"
pfc::string8 remote_prefixes();      // default ""

} // namespace config
} // namespace mtags
