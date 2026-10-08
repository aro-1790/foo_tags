#include "writer.h"

#include "text.h"

#include "config.h"
#include "log.h"
#include "media.h"

#include <string>
#include <vector>

namespace mtags {

const char* const utf8_bom = "\xEF\xBB\xBF";

namespace {

// The original's locator rewrite works on a path with no scheme and "/"
// separators; that is the form both the document and its examples use.
std::string native_form(std::string path) {
    if (starts_with(path, "file://")) {
        path.erase(0, 6);   // leaves the leading "/" of an absolute path
    }
    for (char& c : path) {
        if (c == '\\') {
            c = '/';
        }
    }
    return path;
}

// Splits a "/"-separated path into components, the form the document itself uses.
std::vector<std::string> split_components(const std::string& path) {
    std::vector<std::string> parts;
    size_t start = 0;
    for (;;) {
        const size_t next = path.find('/', start);
        if (next == std::string::npos) {
            parts.push_back(path.substr(start));
            return parts;
        }
        parts.push_back(path.substr(start, next - start));
        start = next + 1;
    }
}

// A member document references its archive from one level up: the original wrote
// "../album.zip|song.wav" there, with forward slashes. A path on another root ("C:"
// against "E:", or a UNC share) has no relative form, so this reports failure and the
// caller falls back to the absolute one.
bool relative_form(const std::string& target, const std::string& base_file,
                   std::string& out) {
    const std::vector<std::string> t = split_components(target);
    std::vector<std::string> b = split_components(base_file);
    if (t.size() < 2 || b.size() < 2) {
        return false;
    }
    b.pop_back();   // the document's own file name is not part of its folder
    size_t common = 0;
    while (common < t.size() && common < b.size()
           && pfc::stricmp_ascii(t[common].c_str(), b[common].c_str()) == 0) {
        ++common;
    }
    if (common == 0) {
        return false;
    }
    out.clear();
    for (size_t i = common; i < b.size(); ++i) {
        out += "../";
    }
    for (size_t i = common; i < t.size(); ++i) {
        out += t[i];
        if (i + 1 < t.size()) {
            out += '/';
        }
    }
    return !out.empty();
}

// The part of a path that is already there: a drive root ("C:") or a UNC share
// ("\\server\share"). Components are only ever created after it. Treating the
// drive as one of them is what turns "make me a subfolder" into "Access denied",
// because the first thing to create would be the root itself.
size_t existing_root_length(const std::string& folder) {
    if (folder.size() >= 2 && folder[1] == ':') {
        const size_t separator = folder.find_first_of("\\/", 2);
        return separator == std::string::npos ? folder.size() : separator;
    }
    if (folder.size() >= 2 && (folder[0] == '\\' || folder[0] == '/')
        && folder[1] == folder[0]) {
        const size_t server = folder.find_first_not_of("\\/", 2);
        if (server == std::string::npos) {
            return folder.size();
        }
        const size_t share = folder.find_first_of("\\/", server);
        return share == std::string::npos ? folder.size() : share;
    }
    return 0;
}

void create_parent_folders(const std::string& path, abort_callback& abort) {
    const size_t slash = path.find_last_of("\\/");
    if (slash == std::string::npos) {
        return;
    }
    const std::string folder = path.substr(0, slash);
    if (folder.empty() || filesystem::g_is_valid_directory(folder.c_str(), abort)) {
        return;
    }

    // One component at a time, in the order the original created them - each step
    // is traced, and a failure is fatal rather than silently ignored. A component
    // starts at the first character after a separator, so neither the drive ("C:")
    // nor a separator on its own is ever a folder to create.
    try {
        size_t start = existing_root_length(folder);
        while (start < folder.size()) {
            start = folder.find_first_not_of("\\/", start);
            if (start == std::string::npos) {
                break;
            }
            const size_t next = folder.find_first_of("\\/", start);
            const size_t end = (next == std::string::npos) ? folder.size() : next;
            const std::string partial = folder.substr(0, end);
            if (!filesystem::g_is_valid_directory(partial.c_str(), abort)) {
                log_verbose("Creating folder %s", partial.c_str());
                filesystem::g_create_directory(partial.c_str(), abort);
            }
            if (next == std::string::npos) {
                break;
            }
            start = next;
        }
    }
    catch (...) {
        // The original's handler logs this and returns to its normal exit - the
        // exception is swallowed, so a folder that cannot be made is abandoned and
        // the write continues to its own guard.
        log_error("FATAL: Error in creating folder %s", folder.c_str());
        return;
    }
}

} // namespace

namespace {

// A reference the document cannot express relatively: the original wrote those with
// forward slashes and a leading one - "C:\folder\file.wav" became
// "/C:/folder/file.wav" - which is what its own files contain.
std::string absolute_form(const std::string& path) {
    std::string out = path;
    for (char& c : out) {
        if (c == '\\') {
            c = '/';
        }
    }
    if (out.empty() || out[0] != '/') {
        out.insert(out.begin(), '/');
    }
    return out;
}

} // namespace

std::string make_locator(const std::string& tags_path, const std::string& media_path) {
    log_verbose("tags_path: %s", tags_path.c_str());
    log_verbose("path: %s", media_path.c_str());

    std::string path = media_path;

    // An archive member is referenced by its member path, not by the archive.
    const char* const member = "|file://";
    if (starts_with(path, "unpack://")) {
        const size_t bar = path.find(member);
        if (bar != std::string::npos) {
            path = path.substr(bar + 1);
        }
    }

    std::string locator;
    if (starts_with(path, "file://")) {
        // Only the part before "|" is a path; a tail there is a subsong (or a
        // further member) and is kept exactly as it was.
        const std::string body = path.substr(6);
        const size_t bar = body.find('|');
        const std::string tail = (bar == std::string::npos) ? std::string()
                                                            : body.substr(bar);
        std::string head = native_form(bar == std::string::npos ? path
                                                               : path.substr(0, 6 + bar));

        if (!config::only_absolute_paths()) {
            // Same folder first - that is the common case, and a plain prefix
            // comparison gets it right without asking anyone.
            const std::string document = native_form(tags_path);
            const size_t slash = document.find_last_of('/');
            const std::string folder =
                (slash == std::string::npos) ? std::string() : document.substr(0, slash + 1);

            if (!folder.empty() && head.size() > folder.size()
                && pfc::stricmp_ascii(head.substr(0, folder.size()).c_str(),
                                      folder.c_str()) == 0) {
                locator = head.substr(folder.size()) + tail;
            }
            else {
                // Anything else needs "up a level" - "../album.zip|song.wav" for a member
                // document. The absolute form stays the fallback, so a reference with no
                // relative form still resolves.
                std::string relative;
                locator = relative_form(head, document, relative) ? relative + tail
                                                                  : absolute_form(head) + tail;
            }
        }
        else {
            locator = absolute_form(head) + tail;
        }
    }
    else {
        locator = path;
    }

    log_verbose("locator: %s", locator.c_str());
    return locator;
}

void write_document(const char* path, const t_document& doc, abort_callback& abort) {
    const std::string text = doc.serialise();

    try {
        create_parent_folders(path, abort);

        file::ptr out;
        filesystem::g_open_write_new(out, path, abort);
        // The original's own files start with a UTF-8 BOM - verified by reading back one
        // it wrote - and its loader strips it again, reporting "file is UTF-8".
        out->write(utf8_bom, 3, abort);
        out->write(text.data(), static_cast<t_size>(text.size()), abort);
    }
    catch (...) {
        log_error("FATAL: cannot write to %s", path);
        throw;
    }
}

t_track build_track(const file_info& info, const std::string& locator) {
    t_tag_set tags = tag_set_from_info(info);   // emits the INFO->TAGS trace

    // The computed locator wins over anything INFO->TAGS found, because "@" is
    // what ties the entry to its media.
    tags[lowercase(tag::at)] = t_tag{ tag::at, t_values{ locator } };

    t_track track;
    track.locator = locator;
    track.tags = std::move(tags);
    return track;
}

} // namespace mtags
