// foo_tags - m-TAGS for foobar2000.
//
// A drop-in recreation of Luigi Mercurio's m-TAGS. A ".tags" file is a JSON
// array describing a playlist: each entry carries the tags for one track and
// points at the media file it stands for, so an album's metadata can live in one
// small text file instead of being written into every media file.

#include <SDK/foobar2000.h>

// Stamped from resource/version.txt into this literal by ./version.sh - do not
// edit by hand. This is the string foobar2000 shows in its component list.
DECLARE_COMPONENT_VERSION("m-TAGS", "1.2.0",
    "The m-TAGS format offers a simple yet powerful solution to the media-metadata separation problem.\n"
    "An m-TAGS file is media-independent: it contains only metadata describing a media source, and a\n"
    "locator identifying the source to which the metadata applies.\n"
    "Original concept and implementation by Luigi Mercurio.");

// A component must keep its file name, and only one component may live per DLL.
VALIDATE_COMPONENT_FILENAME("foo_tags.dll");

// The build pins FOOBAR2000_TARGET_VERSION to 81 (foobar2000 2.0) - FB2K_TARGET in
// build.nmake. Nothing here is written to build below that.
static_assert(FOOBAR2000_TARGET_VERSION >= 81, "m-TAGS targets the foobar2000 2.0 API");

// Same registration as the original: extensions "mtags;tags", singular/plural names.
DECLARE_FILE_TYPE_EX("mtags;tags", "m-TAGS file", "m-TAGS files");
