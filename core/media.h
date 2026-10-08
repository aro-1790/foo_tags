#pragma once

// Projecting a tag set onto a foobar2000 file_info.
//
// Routing comes off the document side: "@" and a string-form DURATION land in the
// INFO context, the four REPLAYGAIN_* names in the replaygain struct, and every
// other tag becomes meta.

#include "format.h"

#include <SDK/foobar2000.h>

namespace mtags {

// Copies every tag of `set` into `info`, emitting the "TAGS->INFO" trace. An empty
// value list is the "[]" exclusion and is skipped.
//
// `clear_first` is the difference between the original's two projections: a tag read
// drops the media's metadata and ReplayGain first, while the per-track dynamic info
// keeps what the decoder has just reported.
void apply_tags(const t_tag_set& set, file_info& info, bool clear_first);

// The pair the original appended after the tags. Both go through meta_set, and
// fb2k drops names beginning with '@', so on a modern foobar2000 they are
// written and then ignored - kept only so the behaviour matches.
void set_referenced(file_info& info, const char* path, unsigned subsong_index);

// What reaches the media a document describes: metadata and the ReplayGain struct.
// "@" and DURATION are filtered by name whatever form they were read in, and the
// media's own tags go, because set_info() replaces them wholesale.
void apply_tags_to_media(const t_tag_set& set, file_info& info);

// The reverse of apply_tags, for writing the document back: every meta field, the
// four REPLAYGAIN_* names from the replaygain struct, and "DURATION"/"@" out of the
// info context. The original's INFO->TAGS step.
t_tag_set tag_set_from_info(const file_info& info);

} // namespace mtags
