#pragma once

// Album art for a ".tags" path.
//
// Art is not stored in the media file. It lives in five sidecar files named after the
// document, one per type:
//
//     <document>.front.bin   .back.bin   .disc.bin   .icon.bin   .artist.bin
//
// The image bytes go into a name nothing else claims, and this component's own
// extractor serves them back - the original's "fake embedded art". The five types
// are foobar2000's own album_art_ids, in the original's order: cover_front,
// cover_back, disc, icon, artist.
//
// The instance also opens an editor on the document's resolved reference and falls back to
// it for any type the sidecars lack, which is how the media's own artwork and folder art
// stay reachable - the original's own chain.
//
// One instance class serves both roles, exactly as the original's did: its editor
// and extractor factory functions allocated the same object and differed only in
// the message they logged.

#include <SDK/foobar2000.h>

#include <string>

namespace mtags {

// The input's class GUID, defined beside the input itself. An art service that
// reports it through album_art_editor_v2::get_guid() is the one foobar2000 will
// associate with this component's paths.
extern const GUID mtags_input_guid;

// The five type suffixes and the type GUIDs they stand for, in the original's
// order. Both arrays have five entries.
extern const char* const g_art_suffixes[5];
extern const GUID* const g_art_types[5];

// The index of `what` in the table above, or -1 for an art type the plugin does
// not know about.
int art_type_index(const GUID& what);

// Copies the art of the document at `tags_path` into the media file's own tags.
// This is the write-back command's last step, and it is what "Do not encapsulate
// album art" turns off. Logs "Saving album art" and the per-type lines.
void encapsulate_album_art(const char* tags_path, const char* media_path,
                           abort_callback& abort);

} // namespace mtags
