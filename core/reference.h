#pragma once

// Turns a tag set's "@" value into a foobar2000 path.

#include "prefixes.h"

#include <string>

namespace mtags {

// What the original ended up with after resolving a locator ("REFERENCED FILE").
struct t_resolved {
    std::string path;   // canonical, with any "|<subsong>" removed
    unsigned index;     // the subsong the locator named, 0 when it named none
    t_media media;      // "local" or "remote"
    // No usable locator at all: the entry has no media, and the original traced the
    // reference as "<INVALID>" rather than refusing the document.
    bool invalid = false;
};

// `locator` is the raw "@" value, `tags_path` the file it was read from - a
// relative reference is relative to that file's folder, so the file must be
// known before any reference can be opened.
//
// Absolute local references start with "/" and lose it; anything else whose
// first ":" precedes its first "/" is treated as a URI and kept whole.
// The result is always a "file://..." path with backslash separators, with any
// "|<subsong>" suffix preserved.
std::string resolve_reference(const std::string& tags_path, const std::string& locator);

// The whole resolve step as the original performed and traced it: rewrite the
// locator, drop the "|<subsong>" suffix (only for "file://" paths - the original
// guarded that step that way, which is why a remote locator never logged
// "archive ref_file"), canonicalise, then classify.
t_resolved resolve(const std::string& tags_path, const std::string& locator);

// The path foobar2000 needs in order to open one member of an archive. A
// document stores the "<archive>|<member>" form; that is not a path the
// filesystem layer will parse, so it has to be rewritten into the packed form
// ("unpack://zip|73|file://...zip|member").
std::string packed_unpack_path(const std::string& archive, const std::string& member);

} // namespace mtags
