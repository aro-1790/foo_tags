#include "format.h"

#include "text.h"

#include "config.h"
#include "log.h"

#include <json/json.h>

namespace mtags {

const char* const tag::at               = "@";
const char* const tag::referenced_file  = "@REFERENCED_FILE";
const char* const tag::referenced_index = "@REFERENCED_INDEX";
const char* const tag::duration         = "DURATION";
const char* const tag::replaygain_album_gain = "REPLAYGAIN_ALBUM_GAIN";
const char* const tag::replaygain_track_gain = "REPLAYGAIN_TRACK_GAIN";
const char* const tag::replaygain_album_peak = "REPLAYGAIN_ALBUM_PEAK";
const char* const tag::replaygain_track_peak = "REPLAYGAIN_TRACK_PEAK";

const char* replaygain_name(const char* name) {
    if (pfc::stricmp_ascii(name, tag::replaygain_album_gain) == 0) {
        return tag::replaygain_album_gain;
    }
    if (pfc::stricmp_ascii(name, tag::replaygain_track_gain) == 0) {
        return tag::replaygain_track_gain;
    }
    if (pfc::stricmp_ascii(name, tag::replaygain_album_peak) == 0) {
        return tag::replaygain_album_peak;
    }
    if (pfc::stricmp_ascii(name, tag::replaygain_track_peak) == 0) {
        return tag::replaygain_track_peak;
    }
    return nullptr;
}

bool is_replaygain_tag(const char* name) {
    return replaygain_name(name) != nullptr;
}

namespace {

// The format's own rendering puts three spaces per level and " : " between a name
// and its value; the helpers for that live below.

// The original read its values through jsoncpp's own predicates - isString, isArray,
// isObject - and reported one it could not use with one of its two format strings.
// A value that is neither a string nor an array of strings is rejected rather than
// stringified, because silently "fixing" a document would hide a typo. The numbers
// are 1-based, as the original's own message read "[1].TITLE [1]" for the first
// element of the first entry's TITLE. The canonical step returns false rather than
// throwing, which is why these do too; the caller adds the second line.
bool read_values(const Json::Value& value, size_t index, const std::string& key,
                 t_values& out, bool& array_form) {
    out.clear();
    array_form = false;

    if (value.isString()) {
        out.push_back(value.asString());
        return true;
    }

    if (value.isArray()) {
        array_form = true;
        out.reserve(value.size());
        for (Json::ArrayIndex i = 0; i < value.size(); ++i) {
            const Json::Value& element = value[i];
            if (!element.isString()) {
                log_error("ERROR: m-TAGS [%s].%s [%s] is not a String",
                          pfc::format_uint(index + 1).get_ptr(), key.c_str(),
                          pfc::format_uint(i + 1).get_ptr());
                return false;
            }
            out.push_back(element.asString());
        }
        return true;
    }

    log_error("ERROR: m-TAGS [%s].%s is not a String",
              pfc::format_uint(index + 1).get_ptr(), key.c_str());
    return false;
}

// No UTF-8 repair: the original read and wrote string bytes verbatim and jsoncpp
// does the same, which is exactly why its own documents can hold bytes that are
// not valid UTF-8.

} // namespace

t_document t_document::parse(const std::string& text, const char* source_name) {
    log_verbose("parsing json");

    // Emitted as one message, matching the original's "%s (%d chars)": the
    // console prefixes only the first line, which is why its trace shows the
    // count on a line with no "m-TAGS:: ".
    log_verbose("%s (%d chars)", text.c_str(), static_cast<int>(text.size()));

    // jsoncpp's reader, called as the original called it - including collectComments -
    // and, exactly as there, ITS RESULT IS NOT EXAMINED. A file that will not parse
    // leaves a null root, which the canonical step below reports as "not a JSON
    // Array". The original printed nothing for a parse failure and jsoncpp's own
    // message is never shown.
    Json::Value root;
    {
        Json::Reader reader;
        reader.parse(text.data(), text.data() + text.size(), root, true);
    }

    log_verbose("making canonical tags");

    if (!root.isArray()) {
        // Both the specific fault and the fact that the file as a whole could not be
        // used, then an empty document - the original's item had no subsongs.
        log_error("ERROR: m-TAGS is not a JSON Array");
        log_error("ERROR: invalid m-TAGS file: %s", source_name);
        return t_document{};
    }

    // The size check comes before the entries are appended, which is the order the
    // original traced them in.
    log_verbose("checking tags size");

    if (root.size() == 0) {
        // An empty array is not a failure: the original logged it and carried on
        // with a single empty tag set.
        log_error("Empty m-TAGS file: %s", source_name);
        t_document empty;
        empty.m_tracks.push_back(t_track{});
        return empty;
    }

    log_verbose("appending tags");

    t_document doc;
    t_tag_set inherited;
    const std::string at_key = lowercase(tag::at);

    for (Json::ArrayIndex index = 0; index < root.size(); ++index) {
        const Json::Value& entry = root[index];
        if (!entry.isObject()) {
            log_error("ERROR: m-TAGS [%s] is not a JSON Object",
                      pfc::format_uint(index + 1).get_ptr());
            log_error("ERROR: invalid m-TAGS file: %s", source_name);
            return t_document{};
        }

        t_track track;
        t_tag_set merged = inherited;
        bool have_locator = false;

        // jsoncpp hands the names back in its own order, which is the order the
        // original walked.
        const Json::Value::Members names = entry.getMemberNames();
        for (const std::string& key : names) {
            const Json::Value& value = entry[key];

            // A "@" written as an array is not a locator. The original kept the value
            // as ordinary metadata ("<@>" in the Properties panel) but produced no
            // reference from it: track 2 of its own console showed "Invalid path: "
            // followed by "REFERENCED FILE: <INVALID> [<INVALID>]", and that entry
            // could not be read. It is *present*, so the preceding locator is not
            // inherited - the difference from "[]", which is erased below.
            if (key == tag::at) {
                if (value.isArray()) {
                    have_locator = true; // present, but unusable: no inheritance
                }
                else {
                    if (!value.isString()) {
                        log_error("ERROR: m-TAGS [%s].%s is not a String",
                                  pfc::format_uint(index + 1).get_ptr(), key.c_str());
                        log_error("ERROR: invalid m-TAGS file: %s", source_name);
                        return t_document{};
                    }
                    track.locator = value.asString();
                    have_locator = true;
                    continue;
                }
            }

            t_values values;
            bool array_form = false;
            if (!read_values(value, index, key, values, array_form)) {
                log_error("ERROR: invalid m-TAGS file: %s", source_name);
                return t_document{};
            }
            const std::string canon = lowercase(key);
            if (values.empty()) {
                // "[]" - excluded here, and it stays excluded in later sets
                // until they write the tag again.
                merged.erase(canon);
            } else {
                // The name kept is the plugin's spelling, not the document's: the
                // original upper-cased names as it read them, which is why the
                // files it wrote carry upper-case keys whatever the input used.
                // The JSON form is kept too: it is what tells a DURATION entry
                // whether it is the length or just a tag called DURATION.
                merged[canon] = t_tag{ uppercase(key), std::move(values), array_form };
            }
        }

        // "@" is mandatory in the first set and optional afterwards, where it
        // repeats the media file of the preceding set. With nothing to inherit - a
        // first set without one - the entry simply keeps no locator: the original
        // accepted that and reported the reference as "<INVALID>", so this must not
        // refuse the document.
        if (!have_locator) {
            const auto prev = inherited.find(at_key);
            if (prev != inherited.end()) {
                track.locator = prev->second.values.front();
            }
        }
        // The set carries the resolved locator so that the projection can put it in
        // the info context. An unusable "@" keeps its own array-form entry instead:
        // that is what shows as "<@>", and what a save writes back.
        if (!track.locator.empty()) {
            merged[at_key] = t_tag{ tag::at, t_values{ track.locator } };
        }

        track.tags = std::move(merged);
        inherited = track.tags;
        doc.m_tracks.push_back(std::move(track));
    }

    return doc;
}

t_document t_document::load(const char* path) {
    // One pair per read, decided by the UTF-8 BOM and logged after the file has been
    // read. Verified against the original's console: a valid document without a BOM
    // prints "file is NOT UTF-8" (uppercase "NOT " is the DLL's literal), one with a
    // UTF-8 BOM prints "file is UTF-8", and a UTF-16 file is not sniffed at all - it
    // just fails to parse. The pair repeats across phases because each phase reads the
    // document again, not within one read.

    std::string text;
    try {
        file::ptr stream;
        filesystem::g_open_read(stream, path, fb2k::noAbort);

        const t_filesize size = stream->get_size(fb2k::noAbort);
        if (size > 0) {
            text.resize(static_cast<size_t>(size));

            // read() is allowed to come back short; fill the buffer properly.
            size_t done = 0;
            while (done < text.size()) {
                const t_size got = stream->read(
                    &text[done], static_cast<t_size>(text.size() - done), fb2k::noAbort);
                if (got == 0) {
                    break;
                }
                done += got;
            }
            text.resize(done);
        }
    }
    catch (...) {
        // The file could not be read at all, and the original treated that exactly like
        // an empty document: it reported the failure and then carried on with empty
        // text, which becomes the "[{}]" substitute below. Verified against its own
        // console, which for an unreadable document shows "File does not exist: <path>"
        // followed by the usual encoding pair, "Empty or absent m-TAGS file" and
        // "[{}] (4 chars)". Nothing is thrown here.
        //
        // The absent case is already reported by the input before it calls this, so the
        // line is only emitted here when the path exists but cannot be opened - which is
        // what the original's own check did with a permission-denied file.
        if (filesystem::g_exists(path, fb2k::noAbort)) {
            log_verbose("File does not exist: %s", path);
        }
    }

    // A UTF-8 BOM is not part of the JSON. The original measured the document
    // without it - its character count for a BOM-prefixed file is three less
    // than the file's size - and reported the encoding it found first.
    const bool has_bom = text.size() >= 3
                      && static_cast<unsigned char>(text[0]) == 0xEF
                      && static_cast<unsigned char>(text[1]) == 0xBB
                      && static_cast<unsigned char>(text[2]) == 0xBF;
    // The two arguments are the DLL's own: an empty string, and "NOT " at
    // 0x10065774 (uppercase - the only standalone one in the pool).
    log_verbose("file is %sUTF-8", has_bom ? "" : "NOT ");
    log_verbose("checking length");
    if (has_bom) {
        text.erase(0, 3);
    }

    if (text.empty()) {
        // Reported, then treated as an empty document rather than failing: the
        // original went on to substitute the literal "[{}]".
        log_error("Empty or absent m-TAGS file: %s", path);
        text = "[{}]";
    }

    return parse(text, path);
}

void t_document::clear_tags() {
    // "Remove all tags" takes the metadata and leaves the document's own locating
    // information: "@" says which media the entry describes, and DURATION is the
    // baked length the format keeps - which is why the original's own files still
    // carry a DURATION per entry after a full removal. Everything else goes.
    const std::string at_key = lowercase(tag::at);
    const std::string duration_key = lowercase(tag::duration);
    for (t_track& track : m_tracks) {
        t_tag_set kept;
        for (const std::string& key : { at_key, duration_key }) {
            const auto found = track.tags.find(key);
            if (found == track.tags.end()) {
                continue;
            }
            // Only DURATION follows the form rule here: an array-form DURATION is
            // metadata and a full removal takes it. "@" is kept either way - it is the
            // entry's media, and the array form is the only record of it.
            if (key == duration_key && found->second.array_form) {
                continue;
            }
            kept[key] = found->second;
        }
        track.tags = std::move(kept);
    }
}

namespace {

// No hand-written renderer here any more. The document goes through jsoncpp's own
// writer - the same one the original used - so the quoting, the array spacing and the
// line breaking are the library's rather than ours.

} // namespace

std::string t_document::serialise() const {
    Json::Value root(Json::arrayValue);
    t_tag_set previous;
    const std::string at_key = lowercase(tag::at);

    for (const t_track& track : m_tracks) {
        Json::Value entry(Json::objectValue);

        // A tag that has gone away has to be written as "[]", or the reader
        // would simply inherit it again from the preceding set.
        for (const auto& kv : previous) {
            if (kv.first == at_key) { continue; }
            if (track.tags.find(kv.first) == track.tags.end()) {
                entry[kv.second.name] = Json::Value(Json::arrayValue);
            }
        }

        for (const auto& kv : track.tags) {
            // An array-form "@" is metadata, and it is the only record of that
            // entry's media, so it is written (as a string) rather than skipped like
            // the resolved locator entry it stands in for.
            if (kv.first == at_key && !kv.second.array_form) { continue; }

            // Values the reader would carry forward are omitted; that is the
            // whole point of the shorthand. "Always write all tags for each
            // source" turns the shorthand off and spells every value out.
            // The form is inherited as well, so a value that matches while its
            // form differs is still written out.
            const auto prev = previous.find(kv.first);
            if (!config::always_write_all_tags()
                && prev != previous.end() && prev->second.values == kv.second.values
                && prev->second.array_form == kv.second.array_form) {
                continue;
            }

            if (kv.second.values.size() == 1) {
                entry[kv.second.name] = kv.second.values.front();
            } else {
                Json::Value array(Json::arrayValue);
                for (const std::string& v : kv.second.values) {
                    array.append(v);
                }
                entry[kv.second.name] = array;
            }
        }

        // The locator is written as a string even where the document wrote it as an
        // array, so a save turns the array form back into a normal reference. The
        // original soft-locked at this point instead: its own reference field for such
        // an entry held nothing, which it reported as "Invalid path: " followed by
        // "REFERENCED FILE: <INVALID> [<INVALID>]".
        if (!track.locator.empty()) {
            entry[tag::at] = track.locator;
        }

        root.append(entry);
        previous = track.tags;
    }

    // No BOM here: write_document writes that, and the original's files start with one
    // (verified by reading back a document it wrote).
    //
    // The document is rendered by jsoncpp's own writer - the original's call was
    // Value::toStyledString(), which is StyledWriter in the jsoncpp it used. 1.9.x
    // changed toStyledString() to StreamWriterBuilder (tab indent, "name":value), so
    // StyledWriter is called directly; it is the same in every version: three spaces
    // per level, " : ", [] and {}, a trailing newline, and a single-line scalar array
    // as [ "a", "b" ].
    Json::StyledWriter writer;
    return writer.write(root);
}

namespace {

// The verbose tag-set dump, rendered by the SAME writer the file goes through, which
// is what the original did - and why its console shows an array as "[ \"214.09\" ]".
// Values keep the form they were read in, so an array-form single value prints as an
// array rather than as a plain string: verified against the original's console.
// StyledWriter ends its document with a newline, and the caller's line supplies one,
// so that trailing newline is dropped.
std::string dump_tag_set(const t_tag_set& tags) {
    Json::Value object(Json::objectValue);
    for (const auto& kv : tags) {
        const t_tag& t = kv.second;
        if (t.array_form || t.values.size() != 1) {
            Json::Value array(Json::arrayValue);
            for (const std::string& v : t.values) {
                array.append(v);
            }
            object[t.name] = array;
        }
        else {
            object[t.name] = t.values.front();
        }
    }

    Json::StyledWriter writer;
    std::string out = writer.write(object);
    if (!out.empty() && out[out.size() - 1] == '\n') {
        out.erase(out.size() - 1);
    }
    return out;
}

} // namespace

std::string render_tag_set(const t_tag_set& tags) {
    return dump_tag_set(tags);
}

}
