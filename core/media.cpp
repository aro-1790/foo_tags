#include "media.h"

#include "text.h"

#include "config.h"
#include "log.h"

#include <cstdlib>
#include <string>

namespace mtags {

namespace {

// The original traced the tag set as a JSON-like object in the format's own
// style, as one message - the console prefixes only the first line, which is why
// its continuation lines carry no "m-TAGS:: ".
void trace_tag_set(const t_tag_set& set) {
    log_verbose("TAGS->INFO");
    log_verbose("%s\n", render_tag_set(set).c_str());
}

// The info dumper: the SDK's file_info::to_console(), copied minus its tail. One
// line each - "File info dump:", the length while there is one, then every metadata
// value and every technical entry. The length is pfc::format_time_ex with 6
// decimals (2:38.386667 beside the document's own 158.3866667); the SDK's
// "Meta is blank" branch and its four "RG ..." lines are not in the original's DLL,
// and none of these lines carries a prefix, because it is a plain console print.
void dump_file_info(const file_info& info) {
    console::print("File info dump:");

    const double length = info.get_length();
    if (length > 0) {
        pfc::string_formatter line;
        line << "Duration: " << pfc::format_time_ex(length, 6);
        console::print(line.get_ptr());
    }

    const t_size fields = info.meta_get_count();
    for (t_size i = 0; i < fields; ++i) {
        const char* name = info.meta_enum_name(i);
        const t_size values = info.meta_enum_value_count(i);
        for (t_size v = 0; v < values; ++v) {
            pfc::string_formatter line;
            line << "Meta: " << name << " = " << info.meta_enum_value(i, v);
            console::print(line.get_ptr());
        }
    }

    const t_size entries = info.info_get_count();
    for (t_size i = 0; i < entries; ++i) {
        pfc::string_formatter line;
        line << "Info: " << info.info_enum_name(i) << " = " << info.info_enum_value(i);
        console::print(line.get_ptr());
    }
}

} // namespace

void apply_tags(const t_tag_set& set, file_info& info, bool clear_first) {
    trace_tag_set(set);

    // A tag read starts from a clean slate. This is what keeps the referenced
    // file's own metadata away from the user: a document does not have to say
    // anything about a tag it does not want, because the absence of a tag in the
    // document is the absence of the tag. ReplayGain travels in its own struct, so
    // it has to be reset separately.
    if (clear_first) {
        info.meta_remove_all();
        info.reset_replaygain();
    }

    // First pass: everything that belongs in the file_info's own contexts.
    for (const auto& kv : set) {
        const t_tag& t = kv.second;
        if (t.values.empty()) {
            continue; // the explicit-exclusion "[]" form
        }
        if (is_replaygain_tag(t.name.c_str())) {
            continue; // second pass
        }
        if (!t.array_form && pfc::stricmp_ascii(t.name.c_str(), tag::at) == 0) {
            // The locator reaches the user through the info context, which is
            // why it shows in the read-only Properties panel and not in the
            // Metadata panel. An array-form "@" is metadata instead - the
            // original showed it there as an editable "<@>" field.
            for (const std::string& v : t.values) {
                info.info_set(tag::at, v.c_str());
            }
        }
        else if (!t.array_form && pfc::stricmp_ascii(t.name.c_str(), tag::duration) == 0) {
            // Only the string form is the length. An array-form DURATION is metadata,
            // and the original showed it as an editable "<DURATION>" field.
            for (const std::string& v : t.values) {
                // The document's DURATION doubles as the track's length while
                // nothing else has measured it - a locator that was never opened
                // reports no technical fields at all - and a length the media
                // reported always wins.
                if (info.get_length() <= 0) {
                    const double seconds = atof(v.c_str());
                    if (seconds > 0) {
                        info.set_length(seconds);
                    }
                }
                info.info_set(tag::duration, v.c_str());
            }
        }
        else {
            // Metadata, under the name the plugin uses: the document's spelling is
            // normalised upward, as the original did with its upper-casing helper,
            // so a document that writes "title" still reaches foobar2000 as TITLE.
            // The name is dropped once and then filled value by value, because
            // fb2k's meta_set holds a single value - calling it per value would
            // leave only the last one of a multi-valued tag.
            const std::string name = uppercase(t.name);
            info.meta_remove_field(name.c_str());
            for (const std::string& v : t.values) {
                info.meta_add(name.c_str(), v.c_str());
            }
        }
    }

    // Second pass: ReplayGain goes into the replaygain struct, not into either
    // context, which is why it is invisible to the dump below.
    for (const auto& kv : set) {
        const t_tag& t = kv.second;
        const char* const name = replaygain_name(t.name.c_str());
        if (t.values.empty() || name == nullptr) {
            continue;
        }
        for (const std::string& v : t.values) {
            info.info_set_replaygain_auto(name, v.c_str());
        }
    }

    // After the ReplayGain pass, as the original had it.
    if (config::verbose_logging()) {
        dump_file_info(info);
    }
}

void set_referenced(file_info& info, const char* path, unsigned subsong_index) {
    // The original wrote this pair with slot 0x48 of the file_info vtable, which
    // is info_set_ex - the INFO context, the same place the locator lives. Metadata
    // would be wrong twice over: it shows in the editable Metadata panel, and
    // INFO->TAGS copies every meta field verbatim into every document built from
    // that file_info.
    info.info_set(tag::referenced_file, path);
    info.info_set(tag::referenced_index, pfc::format_uint(subsong_index).get_ptr());
}

void apply_tags_to_media(const t_tag_set& set, file_info& info) {
    // Metadata only. The original built this file_info with reset() followed by
    // copy_meta() from the document's projection and set_replaygain(), so nothing
    // from the info context ever reaches the media file - and the media's own tags
    // go too, because set_info() replaces a file's tags with what it is handed.
    for (const auto& kv : set) {
        const t_tag& t = kv.second;
        if (t.values.empty()) {
            continue; // the explicit-exclusion "[]" form
        }
        if (pfc::stricmp_ascii(t.name.c_str(), tag::at) == 0
            || pfc::stricmp_ascii(t.name.c_str(), tag::duration) == 0) {
            // The locator belongs to the document, and DURATION never reaches the
            // media - verified by writing back a document whose DURATION was written
            // as an array: the media gained its ordinary tags but no DURATION at all.
            // So the name is filtered here whatever form it was read in.
            continue;
        }
        const char* const replaygain = replaygain_name(t.name.c_str());
        if (replaygain != nullptr) {
            for (const std::string& v : t.values) {
                info.info_set_replaygain_auto(replaygain, v.c_str());
            }
            continue;
        }
        // Dropped once, then filled value by value, for the same reason as in
        // apply_tags: meta_set holds a single value. The name is normalised the same
        // way, so the media file ends up carrying the plugin's spelling.
        const std::string name = uppercase(t.name);
        info.meta_remove_field(name.c_str());
        for (const std::string& v : t.values) {
            info.meta_add(name.c_str(), v.c_str());
        }
    }
}

namespace {

// The info context is where "@" and "DURATION" live once a document has been
// read, and only there - a value that a source file carries as an ordinary tag
// must not be mistaken for one of them.
bool info_lookup(const file_info& info, const char* name, std::string& out) {
    const t_size count = info.info_get_count();
    for (t_size i = 0; i < count; ++i) {
        if (pfc::stricmp_ascii(info.info_enum_name(i), name) == 0) {
            out = info.info_enum_value(i);
            return true;
        }
    }
    return false;
}

// `display` is what the document will say; the set is keyed lower-cased because
// tag names are case-insensitive.
void add(t_tag_set& out, const char* display, const std::string& value) {
    const std::string key = lowercase(display);
    t_tag& t = out[key];
    t.name = display;
    t.values.push_back(value);
}

} // namespace

t_tag_set tag_set_from_info(const file_info& info) {
    log_verbose("INFO->TAGS");

    // The original dumped the info it had just read at this point, before converting
    // it to a tag set - the same dumper the projection uses, gated the same way.
    if (config::verbose_logging()) {
        dump_file_info(info);
    }

    t_tag_set out;

    std::string text;
    if (info_lookup(info, tag::at, text) && info.meta_find_ex(tag::at, SIZE_MAX) == SIZE_MAX) {
        add(out, tag::at, text);
    }
    if (info_lookup(info, tag::duration, text)
        && info.meta_find_ex(tag::duration, SIZE_MAX) == SIZE_MAX) {
        add(out, tag::duration, text);
    }

    // Unless "Do not overwrite duration tag" is set, the real length replaces
    // whatever the document said - which is why the original's files carry seven
    // decimal places.
    if (!config::do_not_overwrite_duration()) {
        const double length = info.get_length();
        if (length > 0) {
            const std::string value =
                pfc::format_float(length, 0, 7).get_ptr();
            const std::string key = lowercase(tag::duration);
            t_tag& t = out[key];
            t.name = tag::duration;
            t.values.assign(1, value);
        }
    }

    const t_size metas = info.meta_get_count();
    for (t_size i = 0; i < metas; ++i) {
        const char* name = info.meta_enum_name(i);
        const std::string display = uppercase(name);
        const t_size values = info.meta_enum_value_count(i);
        for (t_size v = 0; v < values; ++v) {
            const char* value = info.meta_enum_value(i, v);
            if (value != nullptr && *value != 0) {
                add(out, display.c_str(), value);
            }
        }
    }

    // The converted set, rendered after the info dump and still inside the
    // "INFO->TAGS" step - the original did it there.
    if (config::verbose_logging()) {
        log_verbose("%s\n", render_tag_set(out).c_str());
    }

    return out;
}

} // namespace mtags
