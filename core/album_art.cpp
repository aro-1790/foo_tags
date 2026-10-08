#include "album_art.h"

#include "text.h"

#include "format.h"
#include "log.h"
#include "reference.h"

#include <string>

namespace mtags {

const char* const g_art_suffixes[5] = { ".front", ".back", ".disc", ".icon", ".artist" };

const GUID* const g_art_types[5] = {
    &album_art_ids::cover_front,
    &album_art_ids::cover_back,
    &album_art_ids::disc,
    &album_art_ids::icon,
    &album_art_ids::artist,
};

int art_type_index(const GUID& what) {
    for (int i = 0; i < 5; ++i) {
        if (*g_art_types[i] == what) {
            return i;
        }
    }
    return -1;
}

namespace {

// Path helpers. The sidecar is named after the document, with the document's own
// "/" convention turned back into a path.
//
// Deliberately not the creator's native_path(), which looks the same but leaves an
// "unpack://" path alone: that guard is there because a source can sit inside an
// archive, and this one is only ever handed a document's own path.
std::string native_path(const std::string& path) {
    std::string s = path;
    if (s.compare(0, 7, "file://") == 0) {
        s.erase(0, 7);
    }
    for (char& c : s) {
        if (c == '/') {
            c = '\\';
        }
    }
    return s;
}

album_art_data_ptr read_file(const char* path, abort_callback& abort) {
    file::ptr stream;
    filesystem::g_open_read(stream, path, abort);

    const t_filesize size = stream->get_size(abort);
    if (size == 0 || size > 64u * 1024u * 1024u) {
        return nullptr;
    }

    pfc::array_t<t_uint8> buffer;
    buffer.set_size(static_cast<t_size>(size));
    stream->read_object(buffer.get_ptr(), static_cast<t_size>(size), abort);
    return album_art_data_impl::g_create(buffer.get_ptr(), static_cast<t_size>(size));
}

void write_file(const char* path, album_art_data::ptr data, abort_callback& abort) {
    file::ptr stream;
    filesystem::g_open_write_new(stream, path, abort);
    stream->write(data->data(), static_cast<t_size>(data->size()), abort);
}

// ---------------------------------------------------------------------------
// The instance. It answers art queries for a document and records art edits
// until they are committed, when the sidecar files are written or deleted.

// The instance's own artwork is the document's sidecar files, read and written as plain
// files. On top of that it opens an editor for the reference the document points at - the
// original's "Opening internal editor" - and falls back to it when a type is not in the
// sidecars. That is the original's chain, and it is why the media's own artwork and fb2k's
// folder patterns (cover.jpg/png) are reachable at all; it is also what holds the media
// open while the instance lives.
class t_art_instance : public album_art_editor_instance {
public:
    ~t_art_instance() {
        // The original's instance held the same document object the input does, and
        // its destructor runs after the instance's own body - so the document's line
        // comes second, as it did there.
        log_verbose("DESTROYING ALBUM ART EDITOR INSTANCE");
        log_verbose("DESTROYING TAGS");
    }

    t_art_instance(const char* path, abort_callback& abort) {
        // Same object, constructed before the instance's own body, which is why the
        // original's console shows this line first.
        log_verbose("CREATING TAGS");

        for (int i = 0; i < 5; ++i) {
            m_loaded[i] = false;
            m_pending[i] = false;
        }

        log_verbose("CREATING ALBUM ART EDITOR INSTANCE");

        std::string document = path ? path : "";

        // The original dropped the last character of a path ending in ".mtags*"
        // before using it as the document, so that shape is handled the same way.
        log_verbose("Loading locator");
        if (ends_with_ci(document, ".mtags*") && !document.empty()) {
            document.erase(document.size() - 1);
        }
        m_document = document;

        // The original resolved the document's first entry here, and the reader's traces
        // appear inside this block in its console. Its result is the reference whose own
        // editor is opened next.
        try {
            const t_document doc = t_document::load(m_document.c_str());
            if (doc.get_count() > 0) {
                m_ref = resolve(m_document, doc[0].locator);
            }
        }
        catch (...) {
            // A document that cannot be read simply has no art.
        }

        // "Opening internal editor": the original opened an editor for that reference, and
        // only when it had one - its own check being that a path was present. A failure to
        // open is silent there too: the query simply ends up with no fallback.
        if (!m_ref.path.empty()) {
            log_verbose("Opening internal editor");
            try {
                m_internal = album_art_editor::g_open(nullptr, m_ref.path.c_str(), abort);
            }
            catch (...) {
                m_internal.release();
            }
        }
        (void)abort;
    }

    album_art_data_ptr query(const GUID& what, abort_callback& abort) override {
        const int type = art_type_index(what);
        // The original logged the sidecar's own suffix here - "Quering .front" - not the
        // SDK's type name.
        log_verbose("Quering %s", type >= 0 ? g_art_suffixes[type]
                                            : album_art_ids::name_of(what));

        if (type >= 0) {
            if (!m_loaded[type]) {
                const std::string path = sidecar(type);
                if (filesystem::g_exists(path.c_str(), abort)) {
                    log_verbose("Reading %s", path.c_str());
                    m_art[type] = read_file(path.c_str(), abort);
                }
                m_loaded[type] = true;
            }
            if (m_art[type].is_valid() && m_art[type]->size() > 0) {
                return m_art[type];
            }
        }

        // "Trying internal editor": with no sidecar for this type the original asked the
        // reference's own editor, and reported not-found only when there was no editor to
        // ask. The fallback's own failure propagates unwrapped, as it did there - which is
        // why the media's own artwork, and folder art, survive a write here.
        if (m_internal.is_valid()) {
            log_verbose("Trying internal editor");
            return m_internal->query(what, abort);
        }

        log_verbose("Album art not found");
        throw exception_album_art_not_found();
    }

    void set(const GUID& what, album_art_data_ptr data, abort_callback& abort) override {
        log_verbose("Setting %s image", album_art_ids::name_of(what));

        const int type = art_type_index(what);
        if (type < 0) {
            return;
        }
        m_art[type] = data;
        m_loaded[type] = true;
        m_pending[type] = true;
        (void)abort;
    }

    void remove(const GUID& what) override {
        log_verbose("Removing %s image", album_art_ids::name_of(what));

        const int type = art_type_index(what);
        if (type < 0) {
            return;
        }
        // An empty entry is what the commit reads as "delete the sidecar".
        m_art[type].release();
        m_loaded[type] = true;
        m_pending[type] = true;
    }

    void commit(abort_callback& abort) override {
        log_verbose("Saving images");

        for (int type = 0; type < 5; ++type) {
            if (!m_pending[type]) {
                continue;
            }
            m_pending[type] = false;

            const std::string path = sidecar(type);
            if (m_art[type].is_valid() && m_art[type]->size() > 0) {
                log_verbose("Saving %s", path.c_str());
                try {
                    write_file(path.c_str(), m_art[type], abort);
                }
                catch (...) {
                    log_error("FATAL: cannot write to %s", path.c_str());
                    throw;
                }
            }
            else if (filesystem::g_exists(path.c_str(), abort)) {
                log_verbose("Deleting %s", path.c_str());
                try {
                    filesystem::g_remove(path.c_str(), abort);
                }
                catch (...) {
                    log_error("FATAL: cannot delete %s", path.c_str());
                    throw;
                }
            }
        }
    }

private:
    // The original named the sidecar after the m-TAGS file it belongs to, not after
    // any media: the document's own name with the art type and ".bin" appended, so
    // "!.tags" has "!.tags.front.bin" beside it. That is where it is read from too,
    // and it is why the art follows the document rather than the first track.
    std::string sidecar(int type) const {
        return native_path(m_document) + g_art_suffixes[type] + ".bin";
    }

    std::string m_document;
    // The reference the document's first entry resolves to, and the editor opened on it -
    // the fallback source for any art type the sidecars do not carry.
    t_resolved m_ref;
    album_art_editor_instance_ptr m_internal;
    album_art_data_ptr m_art[5];
    bool m_loaded[5];
    bool m_pending[5];
};

// ---------------------------------------------------------------------------
// Registration. Two services over that one instance class, mirroring the
// original's two factories - which differed only in the line they logged.

bool is_our_art_path(const char* path, const char* extension) {
    (void)path;
    if (extension == nullptr) {
        return false;
    }
    // The same rule the input uses: stricmp against the literals "tags" and
    // "mtags*" - the star being a literal final character of the extension.
    return pfc::stricmp_ascii(extension, "tags") == 0
        || pfc::stricmp_ascii(extension, "mtags*") == 0;
}

class t_art_extractor_service : public album_art_extractor_v2 {
public:
    bool is_our_path(const char* path, const char* extension) override {
        return is_our_art_path(path, extension);
    }

    album_art_extractor_instance_ptr open(file_ptr, const char* path,
                                         abort_callback& abort) override {
        log_verbose("Creating album art extractor for %s", path);
        return new service_impl_t<t_art_instance>(path, abort);
    }

    GUID get_guid() override { return mtags_input_guid; }
};

class t_art_editor_service : public album_art_editor_v2 {
public:
    bool is_our_path(const char* path, const char* extension) override {
        return is_our_art_path(path, extension);
    }

    album_art_editor_instance_ptr open(file_ptr, const char* path,
                                      abort_callback& abort) override {
        log_verbose("Creating album art editor for %s", path);
        return new service_impl_t<t_art_instance>(path, abort);
    }

    // Naming the input matters: foobar2000 asks every registered editor whether
    // the path is its, so without this another component that claims all paths
    // can answer first and take the art edit somewhere else entirely.
    GUID get_guid() override { return mtags_input_guid; }
};

FB2K_SERVICE_FACTORY(t_art_extractor_service);
FB2K_SERVICE_FACTORY(t_art_editor_service);

} // namespace

void encapsulate_album_art(const char* tags_path, const char* media_path,
                           abort_callback& abort) {
    log_verbose("Saving album art");

    album_art_editor_instance_ptr external;
    album_art_editor_instance_ptr internal;

    // The document's own editor is asked for first, and the media's is not opened
    // until that one is known good: the original checked and gave up here, so a
    // document whose art cannot be reached never shows an internal open attempt.
    bool opened = true;
    try {
        log_verbose("Opening external album art editor instance");
        external = album_art_editor::g_open(nullptr, tags_path, abort);
    }
    catch (...) {
        external.release();
        opened = false;
    }
    if (!opened) {
        log_verbose("ERROR: Cannot get album art editor interface for: %s", tags_path);
        return;
    }
    if (!external.is_valid()) {
        log_verbose("ERROR: Cannot open album art editor instance for: %s", tags_path);
        return;
    }

    opened = true;
    try {
        log_verbose("Opening internal album art editor instance");
        internal = album_art_editor::g_open(nullptr, media_path, abort);
    }
    catch (...) {
        internal.release();
        opened = false;
    }
    if (!opened) {
        log_verbose("ERROR: Cannot get album art editor interface for: %s", media_path);
        return;
    }
    if (!internal.is_valid()) {
        log_verbose("ERROR: Cannot open album art editor instance for: %s", media_path);
        return;
    }

    // The media is made to match the document entry by entry: art the document does not
    // carry is removed from the media rather than left behind. What "the document does not
    // carry" means is the original's chain and not the sidecars alone - the document
    // instance falls back to the reference's own editor ("Trying internal editor"), so the
    // media's own artwork and fb2k's folder patterns (cover.jpg/png) are seen and kept.
    for (int type = 0; type < 5; ++type) {
        const GUID& what = *g_art_types[type];

        album_art_data_ptr data;
        try {
            data = external->query(what, abort);
        }
        catch (...) {
            data.release();
        }

        try {
            // The original's own console shows these two lines carrying the entry's
            // position, not its type name: "Removing album art 1" through "5".
            if (data.is_valid() && data->size() > 0) {
                log_verbose("Setting album art %s", pfc::format_uint(type + 1).get_ptr());
                internal->set(what, data, abort);
            }
            else {
                log_verbose("Removing album art %s", pfc::format_uint(type + 1).get_ptr());
                internal->remove(what);
            }
        }
        catch (...) {
            // Formats that cannot hold a given entry are skipped, not fatal.
        }
    }

    try {
        internal->commit(abort);
    }
    catch (...) {
        // The media's own art could not be opened for writing; the tags have been
        // written by now, so nothing here is fatal to the command.
        log_error("ERROR: Cannot open file for writing: %s", media_path);
    }
}

} // namespace mtags
