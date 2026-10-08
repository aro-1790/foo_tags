#pragma once

// Producing ".tags" documents: the "@" value every entry carries, and the file
// write itself. Shared by the creator commands and the input's tag commit, so a
// document written from either side comes out identical.

#include "format.h"

#include <SDK/foobar2000.h>

#include <string>

namespace mtags {

// The "@" value a document stored at `tags_path` should carry for `media_path`.
//
// "unpack://<archive>|file://<member>" keeps only the member; "file://" then
// loses its first six characters - not seven - which is exactly where the
// leading "/" marking an absolute reference comes from. Separators become "/",
// and unless "Only use absolute paths in references" is on the result is made
// relative to the document's own folder.
std::string make_locator(const std::string& tags_path, const std::string& media_path);

// Serialises `doc` to `path` as UTF-8 with a BOM, creating the parent folder when
// it is missing. Logs the original's "FATAL: cannot write to %s" and rethrows.
void write_document(const char* path, const t_document& doc, abort_callback& abort);

// One document entry for a track: INFO->TAGS, then the locator written over
// whatever "@" INFO->TAGS found. The caller brackets the metadata read with its
// own "Retrieving metadata" / "Retrieved metadata" traces, because only the
// paths that actually read metadata emit them.
t_track build_track(const file_info& info, const std::string& locator);

// The byte order mark both the original's writer and its reader agree on.
extern const char* const utf8_bom;

} // namespace mtags
