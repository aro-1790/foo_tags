#pragma once

// The m-TAGS ".tags" document model, following the published specification.
//
// A .tags file is a JSON array; every element is one tag set. Each set carries
// "@" - the media file it stands for, optionally suffixed with "|<subsong>" -
// plus ordinary tags. Two rules keep these files small:
//
//   * a tag absent from a set takes the value of the same tag in the closest
//     preceding set;
//   * a tag written as an empty array ("[]") is explicitly excluded, and that
//     exclusion cascades into the following sets until the tag is written again.
//
// A tag may be multi-valued - "COMPOSER" : ["A", "B"] - and tag names are
// case-insensitive, so tags are keyed lower-cased with the original spelling
// kept for writing them back out.

#include <SDK/foobar2000.h>

#include <map>
#include <string>
#include <vector>

namespace mtags {

// Names the plugin treats specially instead of passing through verbatim. "@" and
// the "@REFERENCED_*" pair wire a document entry to its media file; the latter two
// are written but then dropped, because fb2k's meta_set ignores names starting with
// '@' - they are vestigial. The REPLAYGAIN_* four are the plugin's own hardcoded
// list, not fb2k's replaygain name helper.
namespace tag {
extern const char* const at;                // "@"
extern const char* const referenced_file;   // "@REFERENCED_FILE"
extern const char* const referenced_index;  // "@REFERENCED_INDEX"
extern const char* const duration;          // "DURATION"
extern const char* const replaygain_album_gain;  // "REPLAYGAIN_ALBUM_GAIN"
extern const char* const replaygain_track_gain;  // "REPLAYGAIN_TRACK_GAIN"
extern const char* const replaygain_album_peak;  // "REPLAYGAIN_ALBUM_PEAK"
extern const char* const replaygain_track_peak;  // "REPLAYGAIN_TRACK_PEAK"
}

// The plugin's own spelling of a REPLAYGAIN_* name, or nullptr for anything else.
// The projection writes that spelling rather than the document's, so a document
// that spells the name differently still reaches the replaygain struct.
const char* replaygain_name(const char* name);

// True for the four REPLAYGAIN_* names above.
bool is_replaygain_tag(const char* name);

using t_values = std::vector<std::string>;

// One tag. An empty value list means "explicitly excluded" - the "[]" form.
// `array_form` records that the document wrote the value as an array rather than a
// string. For the reserved names that is what decides the meaning: as a string, "@"
// is the locator and DURATION is the length; as an array, both are ordinary
// metadata.
struct t_tag {
    std::string name;    // the plugin's own spelling: upper case, as the original stored it
    t_values values;
    bool array_form = false;
};

// Keyed by the lower-cased name, because tag names are case-insensitive.
using t_tag_set = std::map<std::string, t_tag>;

struct t_track {
    // Raw "@" value, e.g. "E:\rips\Album.cue|2".
    std::string locator;
    // Every tag in effect for this set, inherited ones included.
    t_tag_set tags;
};

class t_document {
public:
    // Parses .tags text. A structural problem is reported to the console with the
    // original's wording and then thrown, so the user sees the same message either
    // way. An empty array is reported and becomes a single empty tag set, which is
    // what the original's loader did rather than failing the load.
    static t_document parse(const std::string& text, const char* source_name = "");

    // Reads a .tags file and parses it. A missing or zero-length file is reported
    // as "Empty or absent m-TAGS file: %s" and throws exception_io_data.
    static t_document load(const char* path);

    size_t get_count() const { return m_tracks.size(); }
    const t_track& operator[](size_t idx) const { return m_tracks[idx]; }
    t_track& track_at(size_t idx) { return m_tracks[idx]; }

    // Appends an entry, which is how the creator commands assemble a document
    // before writing it out.
    void add_track(t_track track) { m_tracks.push_back(std::move(track)); }

    // Drops every tag of every entry except the two names that live in the info
    // context - the "@" locator and DURATION - which is what the original's "Remove
    // all tags" leaves behind: the metadata goes, the baked length stays.
    void clear_tags();

    // Writes the compact form back, dropping a tag that repeats the previous
    // set's value and writing "[]" wherever a tag disappears.
    std::string serialise() const;

private:
    std::vector<t_track> m_tracks;
};

// "@" is "<path>" or "<path>|<subsong>"; see mtags_locator.h for splitting it.

// Renders a tag set the way the m-TAGS format writes it - three-space indent,
// "name" : value, an array for a multi-valued tag. This is the form the TAGS->INFO
// trace uses, and the same emitter backs serialise(). Note the separator is
// " : " with a space before the colon, which is why the writer is not a plain
// JSON dump.
std::string render_tag_set(const t_tag_set& tags);

}
