#pragma once

// The two re-sort commands, new with this plugin.
//
// Both are driven by a selection of document entries and both rewrite whole
// documents: the selected entries decide which document is touched and what order
// their slots end up in, and every entry the selection does not cover keeps the
// position it had. "By track number" derives the order from the entries' own tags;
// "reflecting playlist order" takes it from the selection's order, which is the
// playlist's.

#include <SDK/foobar2000.h>

namespace mtags {

// [context] Tagging > Re-sort m-TAGS by track number
void resort_by_track_number(metadb_handle_list_cref items);

// [context] Tagging > Re-sort m-TAGS reflecting playlist order
void resort_by_playlist_order(metadb_handle_list_cref items);

} // namespace mtags
