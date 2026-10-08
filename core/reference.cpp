#include "reference.h"

#include "text.h"

#include "locator.h"
#include "log.h"

#include <cstring>

namespace mtags {

namespace {

std::string with_backslashes(std::string s) {
    for (char& c : s) {
        if (c == '/') {
            c = '\\';
        }
    }
    return s;
}

// A subsong index is decimal. A tail that is anything else is a name inside an
// archive, and the two are told apart the same way the original told them apart.
bool is_number(const std::string& s) {
    if (s.empty()) {
        return false;
    }
    for (char c : s) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    return true;
}

} // namespace

// The archive type is the archive file's extension, lowercased: that is what the
// original took (the tail of the archive path after its last dot) and what
// foobar2000's own archive services call themselves.
std::string packed_unpack_path(const std::string& archive, const std::string& member) {
    std::string path = archive;

    // An archive that is itself inside an archive is encoded the same way in the
    // place the archive path would go, which is what makes archives nest.
    const size_t bar = path.rfind('|');
    if (bar != std::string::npos && !is_number(path.substr(bar + 1))) {
        path = packed_unpack_path(path.substr(0, bar), path.substr(bar + 1));
    }

    // "unpack://zip|73|file://...zip|member": the length is not decoration, it is
    // how the path is parsed back apart.
    return "unpack://" + lowercase(extension_of(path)) + "|"
         + std::to_string(path.size()) + "|" + path + "|" + member;
}

std::string resolve_reference(const std::string& tags_path, const std::string& locator) {
    log_verbose("tags_path: %s", tags_path.c_str());
    log_verbose("locator: %s", locator.c_str());

    std::string path;

    if (!locator.empty() && locator[0] == '/') {
        // Absolute local reference: the leading slash is the whole marker.
        path = locator.substr(1);
    }
    else {
        const size_t colon = locator.find(':', 1);
        const size_t slash = locator.find('/', 1);

        // A ":" that comes first means a URI (or, per the spec, a "first segment"
        // that cannot contain one). Otherwise this is a local reference and is
        // resolved against the folder holding the .tags file.
        if (colon == std::string::npos || (slash != std::string::npos && slash < colon)) {
            const size_t sep = tags_path.find_last_of("\\|/");
            path = tags_path.substr(0, sep + 1) + locator;
        }
        else {
            // A URI, and also an "unpack://" reference: that one already names a
            // foobar2000 path, so it is handed on as it stands below.
            path = locator;
        }

        // A locator that already names a foobar2000 path is returned as it stands.
        if (!starts_with(path, "file://")) {
            log_verbose("path: %s", path.c_str());
            return path;
        }
        path = path.substr(7);
    }

    // Re-attach the scheme with native separators, keeping any "|<subsong>" suffix.
    const size_t bar = path.find('|');
    if (bar == std::string::npos) {
        path = "file://" + with_backslashes(path);
    }
    else {
        path = "file://" + with_backslashes(path.substr(0, bar)) + path.substr(bar);
    }

    log_verbose("path: %s", path.c_str());
    return path;
}

t_resolved resolve(const std::string& tags_path, const std::string& locator) {
    // No usable locator. The original traced the reference as "<INVALID>" and carried
    // on with an entry that simply has no media - its track failed to read, but the
    // document as a whole stayed alive, so this must not throw. The "Invalid path: "
    // line belongs to the case where the document had an "@" it could not use, which
    // only the caller can tell; see t_input::update_reference.
    if (locator.size() < 2) {
        log_error("REFERENCED FILE: %s [%s]", "<INVALID>", "<INVALID>");
        t_resolved out;
        out.path.clear();
        out.index = 0;
        out.media = t_media::remote;
        out.invalid = true;
        return out;
    }

    log_verbose("Resolving locator: %s", locator.c_str());

    const std::string rewritten = resolve_reference(tags_path, locator);
    log_verbose("Resolved path: %s", rewritten.c_str());

    // The tail after the last "|" is the subsong when it is a number, and is
    // simply dropped from the path - the original logged nothing for that case.
    std::string plain = rewritten;
    bool indexed = false;
    const size_t bar = plain.rfind('|');
    if (bar != std::string::npos && is_number(plain.substr(bar + 1))) {
        indexed = true;
        plain = plain.substr(0, bar);
    }

    // Whatever is left may still name a file inside an archive, and that is not
    // a path any filesystem will open: the archive is what gets opened, and the
    // member has to be named to it. This is FUN_10019f00, which is where
    // "archive ref_file" is logged.
    if (starts_with(plain, "file://")) {
        const size_t archive_bar = plain.rfind('|');
        if (archive_bar != std::string::npos) {
            plain = packed_unpack_path(plain.substr(0, archive_bar),
                                       plain.substr(archive_bar + 1));
            log_verbose("archive ref_file: %s", plain.c_str());
        }
    }

    t_resolved out;
    pfc::string8 canonical;
    filesystem::g_get_canonical_path(plain.c_str(), canonical);
    out.path = canonical.c_str();
    log_verbose("Canonical path: %s", out.path.c_str());

    // The subsong comes off the locator, not the canonical path, and is kept as
    // written - 0 means the document named none.
    out.index = indexed ? t_locator::parse(rewritten).index : 0;

    out.media = classify_media(out.path);
    log_verbose("Media is %s", media_name(out.media));

    // Always shown, not verbose - the original used its ungated log helper here.
    log_error("REFERENCED FILE: %s [%s]", out.path.c_str(),
              pfc::format_uint(out.index).get_ptr());
    return out;
}

} // namespace mtags
