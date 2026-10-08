#pragma once

// Turning an m-TAGS "@" locator into something foobar2000 can open.
//
// A locator is "<path>" or "<path>|<subsong>". The subsong is 1-based, and one
// media file may be referenced by several tag sets with different subsongs -
// that is how a single .cue file or archive backs a whole album. The original
// resolves the locator into the canonical path it later publishes as
// "@REFERENCED_FILE", with the subsong alongside it as "@REFERENCED_INDEX".
//
// A locator may also carry one of the URI-style prefixes named under
// Advanced Preferences > Tagging > m-TAGS > Media prefixes, which foobar2000's
// filesystem layer understands; "Local" defaults to "file:|unpack:|cdda:".
// Anything without a known prefix is a plain path.

#include <SDK/foobar2000.h>

#include <string>

namespace mtags {

struct t_locator {
    std::string path;   // the locator without its "|<subsong>" suffix
    unsigned index;     // 0 when the document did not name one

    // Splits a raw "@" value. Anything shorter than two characters is rejected
    // up front, because a one-character locator cannot be a path - that is the
    // "Invalid path: %s" branch the original took before resolving.
    static t_locator parse(const std::string& raw);

    // Applies the 1-based convention and the range check. A subsong past the
    // last tag set throws the same exception_io_data the original threw, after
    // logging "Invalid subsong index: %s".
    unsigned index_within(size_t set_count) const;
};

} // namespace mtags
