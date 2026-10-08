// The m-TAGS input service: what makes a .tags file playable.
//
// A .tags file is not audio; it is a list of tag sets, each naming the media file
// (and subsong within it) that it describes. So this input never decodes
// anything itself - it opens the referenced media as an input and forwards
// everything to it, while supplying the metadata from the document.
//
// That forwarding is why the technical facts about the audio (codec, sample
// rate, bit depth, channel count, bitrate...) still reach foobar2000 even though
// none of it is stored in the .tags file: the inner decoder reports them and
// they are passed straight through, with only the tags coming from the document.
//
// Two behaviours are worth calling out, both reconstructed from the original's
// own traces:
//   * the referenced media is only opened when it is classified local, or when
//     decoding - that is why a remote locator produces no technical fields
//     during the info-read pass;
//   * tags land as meta, but "@" and "DURATION" go to the info context.

#include <SDK/foobar2000.h>

#include "text.h"

#include "format.h"
#include "log.h"
#include "media.h"
#include "reference.h"
#include "writer.h"

#include <cstring>
#include <string>

namespace mtags {

// The input's class GUID. The album art services publish it through
// album_art_editor_v2::get_guid() so that foobar2000 can tie this component's art
// handling to the input that owns the path, the way its own inputs do.
// ("extern" is required: a plain const at namespace scope would have internal
// linkage and the art module could not link to it.)
extern const GUID mtags_input_guid =
    { 0x8a41d7c6, 0x2f5b, 0x4a19, { 0x9e, 0x3c, 0x6b, 0x14, 0xd2, 0x87, 0x50, 0xaf } };

} // namespace mtags

namespace {

using namespace mtags;

const GUID guid_input = mtags_input_guid;

// FUN_1001b820 opened every path through one helper, and that helper dropped the
// final character of a path ending in ".mtags*". The shape comes from the wildcard
// the component registers, so the file it names is the ".mtags" one. The album art
// services apply the same rule to the document they are handed.
std::string openable_path(const std::string& path) {
    if (ends_with_ci(path, ".mtags*")) {
        return path.substr(0, path.size() - 1);
    }
    return path;
}

// FUN_1001ce10: an out-of-range subsong reported the same way a bad locator
// index did, and then threw exception_io_data.
unsigned check_subsong(unsigned subsong, size_t count) {
    if (subsong == 0) {
        return 1;
    }
    if (subsong > count) {
        log_error("Invalid subsong index: %s", pfc::format_uint(subsong).get_ptr());
        throw exception_io_data();
    }
    return subsong;
}

class t_input : public input_stubs {
public:
    // Which interfaces this class implements. The SDK's default decoder interface is
    // input_decoder_v5, whose poll_live_info() replaces get_dynamic_info() and
    // get_dynamic_info_track() - and foobar2000 stops calling those two on any object
    // that implements it, whether or not the implementation does anything. Ours would
    // be the SDK's empty body, so the per-track dynamic info, and the document's tags
    // layered over it, would quietly stop being reached. The original advertised
    // input_decoder_v3, which is also the surface this class overrides, so that is
    // what it declares.
    typedef input_decoder_v3 interface_decoder_t;

    t_input() { log_verbose("CREATING TAGS"); }
    ~t_input() { log_verbose("DESTROYING TAGS"); }

    // ---------------------------------------------------------------- opening

    void open(service_ptr_t<file> hint, const char* path, t_input_open_reason reason,
              abort_callback& abort) {
        // One literal per reason, which is how the original's open helper chose its
        // line as well.
        const char* opening = reason == input_open_decode     ? "Opening %s for decoding"
                            : reason == input_open_info_write ? "Opening %s for writing"
                                                              : "Opening %s for reading";
        log_verbose(opening, path);
        log_verbose("OPENING %s", path);
        // Which of the two ways the original obtained the resource. It logged
        // "path" when it opened the path itself and "filehint" when it was handed
        // an already-open file.
        log_verbose(hint.is_valid() ? "using filehint" : "using path");

        m_tags_path = openable_path(path ? path : "");
        m_reason = reason;

        // FUN_10023540 opened the document by path and reported a path that is not
        // there before attempting the parse. The line is verbose-gated, as the
        // original's was.
        if (!filesystem::g_exists(m_tags_path.c_str(), abort)) {
            log_verbose("File does not exist: %s", m_tags_path.c_str());
        }

        m_doc = t_document::load(m_tags_path.c_str());
        m_edited = m_doc;
        m_current = 0;
        m_dirty = false;
        (void)abort;
    }

    // ------------------------------------------------------------ enumeration

    unsigned get_subsong_count() {
        const unsigned count = static_cast<unsigned>(m_doc.get_count());
        log_verbose("Get subsong count: %s", pfc::format_uint(count).get_ptr());
        return count;
    }

    t_uint32 get_subsong(unsigned index) {
        // Subsongs are 1-based everywhere; the original logged the mapping.
        log_verbose("Get subsong: %s -> %s", pfc::format_uint(index).get_ptr(),
                    pfc::format_uint(index + 1).get_ptr());
        return index + 1;
    }

    // ------------------------------------------------------------- info

    void get_info(t_uint32 subsong, file_info& info, abort_callback& abort) {
        subsong = check_subsong(subsong, m_doc.get_count());
        log_verbose("Reading tags: %s [%s]", m_tags_path.c_str(),
                    pfc::format_uint(subsong).get_ptr());

        activate(subsong, abort);
        info.reset();

        if (m_reason == input_open_decode && m_decoder.is_valid()) {
            m_decoder->get_info(m_ref.index, info, abort);
        }
        else if (m_reader.is_valid()) {
            m_reader->get_info(m_ref.index, info, abort);
        }

        // The document's tags are all there is: everything the referenced file
        // reported under its own tag names has just been dropped, which is the
        // whole point - and this is the step that traces TAGS->INFO.
        apply_tags(m_doc[subsong - 1].tags, info, /*clear_first=*/true);
        set_referenced(info, m_ref.path.c_str(), m_ref.index);
    }

    // The SDK's interface wrapper routes input_info_reader::get_file_stats()
    // through get_stats2() with stats2_legacy, so this is the single entry point
    // for stat requests and where the original's trace lands.
    t_filestats2 get_stats2(uint32_t, abort_callback& abort) {
        log_verbose("Retrieving file stats");
        try {
            t_filestats legacy;
            bool exists = false;
            filesystem::g_get_stats(m_tags_path.c_str(), legacy, exists, abort);
            if (exists) {
                return t_filestats2::from_legacy(legacy);
            }
        }
        catch (...) {
            // Statistics are advisory; a failure here must not break playback.
        }
        return t_filestats2();
    }

    // ------------------------------------------------------------ decoding

    void decode_initialize(t_uint32 subsong, unsigned flags, abort_callback& abort) {
        subsong = check_subsong(subsong, m_doc.get_count());
        log_verbose("Decode initialize");

        activate(subsong, abort);

        if (m_decoder.is_valid()) {
            m_decoder->initialize(m_ref.index, flags, abort);
        }
    }

    bool decode_run(audio_chunk& chunk, abort_callback& abort) {
        return m_decoder.is_valid() && m_decoder->run(chunk, abort);
    }

    void decode_seek(double seconds, abort_callback& abort) {
        log_verbose("Forwarding -> Decode seek");
        if (m_decoder.is_valid()) {
            m_decoder->seek(seconds, abort);
        }
    }

    bool decode_can_seek() {
        log_verbose("Forwarding -> Decode can seek");
        return m_decoder.is_valid() && m_decoder->can_seek();
    }

    bool decode_get_dynamic_info(file_info& out, double& delta) {
        return m_decoder.is_valid() && m_decoder->get_dynamic_info(out, delta);
    }

    bool decode_get_dynamic_info_track(file_info& out, double& delta) {
        if (!m_decoder.is_valid() || !m_decoder->get_dynamic_info_track(out, delta)) {
            return false;
        }
        // What the decoder just reported stays - this call carries what changes
        // during playback - so the document's tags are laid over it rather than
        // replacing it. (FUN_1001aee0, the one projection that does not clear.)
        if (m_current >= 1 && m_current <= m_doc.get_count()) {
            apply_tags(m_doc[m_current - 1].tags, out, /*clear_first=*/false);
        }
        return true;
    }

    void decode_on_idle(abort_callback& abort) {
        if (m_decoder.is_valid()) {
            m_decoder->on_idle(abort);
        }
    }

    void set_logger(event_logger::ptr ptr) {
        log_verbose("Forwarding -> Set logger");
        input_decoder_v2::ptr v2;
        if (m_decoder.is_valid() && m_decoder->service_query_t(v2)) {
            v2->set_logger(ptr);
        }
    }

    void set_pause(bool paused) {
        input_decoder_v3::ptr v3;
        if (m_decoder.is_valid() && m_decoder->service_query_t(v3)) {
            v3->set_pause(paused);
        }
    }

    bool flush_on_pause() {
        log_verbose("Forwarding -> Flush on pause");
        input_decoder_v3::ptr v3;
        return m_decoder.is_valid() && m_decoder->service_query_t(v3) && v3->flush_on_pause();
    }

    // ------------------------------------------------------------- writing
    // The document is the only thing written; the referenced media is never
    // touched, which is the whole point of m-TAGS.

    void retag_set_info(t_uint32 subsong, const file_info& info, abort_callback& abort) {
        subsong = check_subsong(subsong, m_edited.get_count());
        log_verbose("Updating tags");

        t_track& track = m_edited.track_at(subsong - 1);
        const std::string previous = track.locator;

        t_tag_set fresh = tag_set_from_info(info);

        // The locator lives in the info context, so it is easily lost on a
        // round trip. Keep the document's own unless the caller supplied one -
        // which it can, by writing an "@" tag into the metadata panel.
        const std::string at_key = lowercase(tag::at);
        auto at = fresh.find(at_key);
        if (at == fresh.end() && !previous.empty()) {
            t_tag& t = fresh[at_key];
            t.name = tag::at;
            t.values.assign(1, previous);
            at = fresh.find(at_key);
        }

        track.tags = std::move(fresh);
        if (at != track.tags.end() && !at->second.values.empty()) {
            track.locator = at->second.values.front();
        }
        m_dirty = true;
        (void)abort;
    }

    void retag_commit(abort_callback& abort) {
        if (!m_dirty) {
            return;
        }
        log_verbose("Saving tags");

        write_document(m_tags_path.c_str(), m_edited, abort);
        m_doc = m_edited;
        m_dirty = false;
    }

    void remove_tags(abort_callback& abort) {
        // "Removes all tags from this file": the document is the file, and every
        // track in it is one of its tag sets, so all of them are emptied - except
        // for the two info-context names, "@" and DURATION, which is exactly what
        // the original leaves behind (a removal in the real component keeps the
        // baked durations and drops everything else).
        m_edited.clear_tags();
        m_dirty = true;
        retag_commit(abort);
    }

    // ---------------------------------------------------------- registration

    static bool g_is_our_content_type(const char* content_type) {
        (void)content_type;
        return false;
    }

    static bool g_is_our_path(const char* path, const char* extension) {
        (void)path;
        // FUN_1001b5e0 / FUN_1001b620 compare the extension with stricmp against the
        // two literals, so "mtags*" is an exact extension whose last character is a
        // literal star - not a wildcard, and not a prefix. The path is then opened
        // with that final character dropped ("x.mtags*" names the document "x.mtags").
        return pfc::stricmp_ascii(extension, "tags") == 0
            || pfc::stricmp_ascii(extension, "mtags*") == 0;
    }

    static GUID g_get_guid() { return guid_input; }
    static const char* g_get_name() { return "m-TAGS"; }

private:
    // FUN_1001ba70 - make `subsong` current, resolving its locator and reopening
    // the referenced media when it turns out to be a different file.
    void activate(unsigned subsong, abort_callback& abort) {
        subsong = check_subsong(subsong, m_doc.get_count());
        log_verbose("Opening track %s", pfc::format_uint(subsong).get_ptr());

        // FUN_1001cce0 only re-resolves when the subsong actually changes, which
        // is why starting playback right after reading the tags does not repeat
        // the whole resolve chain.
        if (subsong == m_current && !m_ref.path.empty()) {
            return;
        }

        const std::string previous = m_ref.path;
        // "Invalid path: " was logged only when the entry carried an "@" it could not
        // use - an array-form one, which survives in the tag set. An "@" that was
        // excluded ("[]") or absent produced no such line.
        if (m_doc[subsong - 1].locator.empty()
            && m_doc[subsong - 1].tags.find(tag::at) != m_doc[subsong - 1].tags.end()) {
            log_error("Invalid path: %s", m_doc[subsong - 1].locator.c_str());
        }
        m_ref = resolve(m_tags_path, m_doc[subsong - 1].locator);
        m_current = subsong;

        if (!m_ref.invalid && m_ref.path != previous) {
            close_referenced();
            // Note the guard: remote media is only opened when actually decoding,
            // which is why an info read of a stream collects no technical fields.
            if (m_ref.media == t_media::local || m_reason == input_open_decode) {
                open_referenced(abort);
            }
        }
    }

    // FUN_1001bd40
    void open_referenced(abort_callback& abort) {
        // The same helper opened this one, so the same rule applies to the media
        // path a locator resolved to.
        const std::string target = openable_path(m_ref.path);
        log_verbose(m_reason == input_open_decode ? "Opening %s for decoding"
                                                  : "Opening %s for info read",
                    target.c_str());
        try {
            if (m_reason == input_open_decode) {
                input_entry::g_open_for_decoding(m_decoder, nullptr, target.c_str(), abort);
            }
            else {
                input_entry::g_open_for_info_read(m_reader, nullptr, target.c_str(), abort);
            }
        }
        catch (...) {
            // Reported and then swallowed, as the original did: its console shows this
            // line and then continues, with the decoder still reporting "Opened using:
            // m-TAGS" and only the analysis failing. Rethrowing made foobar2000 report
            // "Unable to open item for playback". The decoder is is_valid()-guarded
            // everywhere below, so an unopenable entry simply has nothing to decode.
            //
            // The second number is the resolved locator's own subsong, not the entry's
            // position: the original printed "[0]" for a locator with no "|<n>" and
            // "[2]" for one naming subsong 2.
            log_error("Cannot open referenced file as media source: %s [%s]",
                      m_ref.path.c_str(), pfc::format_uint(m_ref.index).get_ptr());
        }
    }

    void close_referenced() {
        m_decoder.release();
        m_reader.release();
    }

    t_document m_doc;
    t_document m_edited;
    std::string m_tags_path;
    t_resolved m_ref;
    unsigned m_current = 0;
    t_input_open_reason m_reason = input_open_info_read;
    input_decoder::ptr m_decoder;
    input_info_reader::ptr m_reader;
    bool m_dirty = false;
};

input_factory_t<t_input> g_input_factory;

} // namespace
