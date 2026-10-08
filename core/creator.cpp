#include "creator.h"

#include "text.h"

#include "album_art.h"
#include "config.h"
#include "format.h"
#include "locator.h"
#include "log.h"
#include "media.h"
#include "reference.h"
#include "writer.h"

#include <SDK/archive.h>
#include <SDK/core_api.h>
#include <SDK/fileDialog.h>
#include <SDK/threaded_process.h>

#include <memory>
#include <string>
#include <vector>

namespace mtags {

namespace {

// ---------------------------------------------------------------- path helpers

// The album art module has a native_path() too, and this one is deliberately not
// it: a source can itself sit inside an archive, so an "unpack://" path is left
// standing here instead of being turned into a file path.
std::string native_path(const char* path) {
    std::string s(path ? path : "");
    if (archive_impl::g_is_unpack_path(s.c_str())) {
        // An unpack path is not a file path with a scheme bolted on: both its
        // halves are paths, and one of them is a separator. It is left alone.
        return s;
    }
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

// The "file://" form the original's locator rewrite expects. It worked on
// canonical paths, which carried the scheme; on a modern build the paths handed
// to us do not, so the scheme is put back before the rewrite runs.
std::string canonical_path(const char* path) {
    const std::string s = native_path(path);
    if (archive_impl::g_is_unpack_path(s.c_str())) {
        // Prefixing this one would hide the "unpack://" the rewrite looks for.
        return s;
    }
    return "file://" + s;
}

std::string folder_of(const std::string& path) {
    const size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

std::string file_name_of(const std::string& path) {
    const size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string without_extension(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

std::string without_trailing_separator(std::string s) {
    while (s.size() > 1 && (s.back() == '\\' || s.back() == '/')) {
        s.pop_back();
    }
    return s;
}

bool exists(const std::string& path) {
    return filesystem::g_exists(path.c_str(), fb2k::noAbort);
}

// A selected item this command can act on is a document, not media.
bool is_document_path(const std::string& path) {
    const std::string ext = uppercase(extension_of(path));
    // Exactly the extensions the input service claims - "tags", or the literal
    // "mtags*" form whose final character the open routine drops.
    return ext == "TAGS" || ext == "MTAGS*";
}

// The folder a path lives in. A file inside an archive is held by the folder that
// holds the archive: taking the folder of the unpack path itself yields a
// truncated URL, which is not a path any filesystem will accept.
std::string folder_for(const std::string& path) {
    if (archive_impl::g_is_unpack_path(path.c_str())) {
        pfc::string8 archive, member;
        if (archive_impl::g_parse_unpack_path(path.c_str(), archive, member)) {
            return folder_for(native_path(archive.get_ptr()));
        }
    }
    return folder_of(native_path(path.c_str()));
}

// ------------------------------------------------------------- source checks

// The original asked the input layer whether a file is audio (FUN_1000593c): it
// walked the input services and asked each one whether the path is its own, which
// is what input_entry::g_is_supported_path() does. Opening the file instead would
// ask a different question - a file whose service claims it but cannot open it is
// still a source, and opening one inside an archive parses the archive.
// Asked of every input service, some of which open files - or archives - to
// answer, and any of which may throw. The question is "is this audio?" and a
// failure to answer is a no, never a reason to take the app down.
bool is_audio_file(const std::string& path) {
    // ".tags" is claimed by our own input service, so an existing document is a
    // source like any other: that is what the original's question to the input layer
    // answers, and it is why a folder holding its own document still reaches the
    // writer.
    try {
        return input_entry::g_is_supported_path(path.c_str());
    }
    catch (...) {
        return false;
    }
}

// "Excluded extensions" holds a "|"-separated list. The original wrapped the
// extension in the same pipe and searched the list as text.
bool extension_excluded(const std::string& path) {
    const std::string ext = extension_of(path);
    log_verbose("Checking exclusion: %s", uppercase(ext).c_str());

    const pfc::string8 configured = config::excluded_extensions();

    const std::string list = "|" + uppercase(configured.get_ptr()) + "|";
    const std::string wrapped = "|" + uppercase(ext) + "|";
    return list.find(wrapped) != std::string::npos;
}

// "Folder-wide m-TAGS file name", "!" by default.
std::string folder_wide_file(const std::string& folder) {
    pfc::string8 name = config::folder_wide_name();
    if (name.is_empty()) {
        name = "!";
    }
    // The setting is a file name, so one that already ends in ".tags" is used as
    // it stands rather than collecting a second extension.
    std::string file = name.get_ptr();
    if (!ends_with_ci(file, ".tags")) {
        file += ".tags";
    }
    return folder + "\\" + file;
}

// The original tested two names for a per-source document under the ONE message
// "m-TAGS file with same name exists. Skipping %s": the decomp shows a single skip
// fed by two existence checks (out.cpp:33050ff). Only ".tags" is ever written; the
// other candidate is why a ".tag" file beside a source made that source look
// already documented. Reproduced as the original's own behaviour.
std::string per_source_file(const std::string& folder, const std::string& source,
                            const char* extension = ".tags") {
    const std::string name = file_name_of(source);
    std::string base = without_extension(name);
    if (config::keep_source_extensions()) {
        base += "." + extension_of(name);
    }
    return folder + "\\" + base + extension;
}

// ----------------------------------------------------------------- archives

// "Do not parse archives" is the switch; archive_v2 is the weeder-outer the SDK
// tells you to ask before listing.
bool is_archive_file(const std::string& path) {
    try {
        archive_v2::ptr archive = archive_v2::tryGet(path.c_str());
        return archive.is_valid() && archive->is_our_archive(path.c_str());
    }
    catch (...) {
        return false;
    }
}

// The archive listing reports its entries as unpack paths. Anything that is not
// one is a member name relative to the archive root, and is addressed through
// the archive so that it stays openable.
std::string member_path(const std::string& archive_path, const std::string& url) {
    if (archive_impl::g_is_unpack_path(url.c_str())) {
        return url;
    }
    return packed_unpack_path(canonical_path(archive_path.c_str()), url);
}

std::vector<std::string> list_archive(const std::string& path, abort_callback& abort) {
    std::vector<std::string> members;
    try {
        archive_v2::ptr archive = archive_v2::tryGet(path.c_str());
        if (!archive.is_valid()) {
            return members;
        }
        archive->archive_list_(
            path.c_str(), nullptr,
            [&path, &members](const char* url, const t_filestats&, file::ptr) {
                members.push_back(member_path(path, url));
            },
            false, abort);
    }
    catch (...) {
        // A damaged archive is reported by its reader; the scan carries on with
        // whatever else the folder holds. The original had exactly one listing-failure
        // line - an archive is listed as part of the folder scan - so it is that one.
        log_error("Error while listing folder %s", path.c_str());
        members.clear();
    }
    return members;
}

// ------------------------------------------------------------------ documents

// Progress reporting is optional: the commands that have a dialog give it one,
// the ones that do not (a single document from a selection) pass nothing.
struct t_progress {
    threaded_process_status* status = nullptr;

    void item(const std::string& path) {
        if (status) {
            status->set_item(path.c_str());
        }
    }
    void step(t_size done, t_size total) {
        if (status) {
            status->set_progress(done, total);
        }
    }
};

// The input layer's view of one source: how many subsongs it has, what their
// identifiers are, and their metadata. The original opened exactly this - an info
// reader per source, not a database lookup - which is also why a file that is not
// in the database yet still yields a complete document.
class t_source_reader {
public:
    bool open(const char* path, abort_callback& abort) {
        try {
            input_entry::g_open_for_info_read(m_reader, nullptr, path, abort);
            return m_reader.is_valid();
        }
        catch (...) {
            return false;
        }
    }

    unsigned count() { return m_reader->get_subsong_count(); }
    unsigned subsong(unsigned index) { return m_reader->get_subsong(index); }
    void info(unsigned id, file_info& out, abort_callback& abort) {
        m_reader->get_info(id, out, abort);
    }

private:
    input_info_reader::ptr m_reader;
};

// The locator rewrite, FUN_10024a60. It is given the source exactly as foobar2000
// reports it; an archive member arrives as "unpack://<type>|<ver>|file://<archive>
// |<member>" and the rewrite reduces that to the archive plus the member, which is
// the containment path the original's own files contain.
std::string locator_for(const std::string& document_path, const std::string& source) {
    try {
        return make_locator(document_path, canonical_path(source.c_str()));
    }
    catch (...) {
        return source;
    }
}

// A track carrying nothing but its locator - what "Ignore source tags" leaves
// behind.
t_track bare_track(const std::string& locator) {
    t_track track;
    track.locator = locator;
    track.tags[lowercase(tag::at)] = t_tag{ tag::at, t_values{ locator } };
    return track;
}

// One source file. A cue sheet, or a media file with chapters, has one subsong
// per track - and gets one document entry per subsong, which is what makes a
// single-file album work. The locator is rewritten once for the file and the
// "|" suffix is added per subsong.
void add_source_entries(t_document& doc, const std::string& document_path,
                        const std::string& source, abort_callback& abort) {
    log_verbose("Processing %s", source.c_str());

    t_source_reader reader;
    if (!reader.open(source.c_str(), abort)) {
        return;
    }

    const unsigned count = reader.count();
    if (count == 0) {
        // Nothing to take from this source. The original traced nothing beyond
        // "Processing" for a source that yields no subsongs.
        return;
    }
    log_verbose("Track count: %s", pfc::format_uint(count).get_ptr());

    const std::string base = locator_for(document_path, source);

    for (unsigned i = 0; i < count; ++i) {
        const unsigned id = reader.subsong(i);
        log_verbose("Subsong: %s", pfc::format_uint(id).get_ptr());

        std::string locator = base;
        if (count > 1 || id != 0) {
            // The suffix is written whenever the identifier alone would be
            // ambiguous - that is, for any multi-subsong file, including its
            // first track.
            locator += "|" + std::string(pfc::format_uint(id).get_ptr());
        }

        if (config::ignore_source_tags()) {
            doc.add_track(bare_track(locator));
            continue;
        }

        // The conversion is what the original bracketed: its "INFO->TAGS" trace,
        // the info dump and the tag-set dump all fall between these two lines.
        log_verbose("Retrieving metadata");
        file_info_impl info;
        bool have_info = false;
        try {
            reader.info(id, info, abort);
            have_info = true;
        }
        catch (...) {
        }
        doc.add_track(have_info ? build_track(info, locator) : bare_track(locator));
        log_verbose("Retrieved metadata");
    }
}

void write_job(const std::string& destination, const std::vector<std::string>& sources,
               abort_callback& abort, t_progress& progress) {
    // The writer's own first line, before it looks at anything: the original logged
    // it here rather than in the commands that call this.
    log_verbose("Saving tag files");

    // The original ignored its own "Overwrite existing m-TAGS files" setting here: an
    // existing document is skipped whatever it says. FUN_10021170 logs this through
    // FUN_100264e0, the same verbose-gated helper as every other creator line.
    if (exists(destination)) {
        log_verbose("File exists: skipping %s", canonical_path(destination.c_str()).c_str());
        return;
    }

    t_document doc;
    const std::string document = canonical_path(destination.c_str());
    for (const std::string& source : sources) {
        progress.item(source);
        add_source_entries(doc, document, source, abort);
    }

    // No empty-source guard here, deliberately: the original reaches its writer with
    // nothing accumulated (FUN_10021170 calls it outside that check) and writes its
    // empty document - the 6-byte scaffold the recorded old-side snapshots show for
    // g1-excluded. Our writer already produces exactly those bytes, verified by the
    // state 2 / 2A / 2B replays.
    log_verbose("Writing m-TAGS file: %s", canonical_path(destination.c_str()).c_str());
    progress.item(destination);
    write_document(destination.c_str(), doc, abort);
}

// One document to write: where it goes and which source files belong in it.
struct t_job {
    std::string destination;
    std::vector<std::string> sources;
};

// One folder to scan, and the folder the documents built from it belong in. The
// two are the same folder unless "in separate folder" was chosen.
struct t_folder_job {
    std::string source;
    std::string destination;
};

t_job& job_for(std::vector<t_job>& jobs, const std::string& destination) {
    for (t_job& job : jobs) {
        if (pfc::stricmp_ascii(job.destination.c_str(), destination.c_str()) == 0) {
            return job;
        }
    }
    jobs.push_back(t_job{ destination, std::vector<std::string>() });
    return jobs.back();
}

// The name of a file inside an archive, taken from the member URL fb2k reports:
// "unpack://<type>|<ver>|file://<archive>|<member>".
std::string member_name_of(const std::string& member) {
    const size_t bar = member.find_last_of('|');
    return bar == std::string::npos ? member : member.substr(bar + 1);
}

// The document a folder source belongs in.
std::string source_document(const std::string& destination_folder, const std::string& file) {
    return config::separate_file_per_source() ? per_source_file(destination_folder, file)
                                              : folder_wide_file(destination_folder);
}

// What an archive's members share: named after the archive itself, beside it, in
// the folder the documents go to. It is a document of its own when the folder owns
// one document, and the folder holding one document per member otherwise.
std::string archive_container(const std::string& destination_folder, const std::string& archive) {
    return destination_folder + "\\" + file_name_of(archive) + ".tags";
}

// A member's own document, by the same naming rule a source file gets - applied to
// the member's name inside the archive rather than to a path.
std::string member_document(const std::string& container, const std::string& member) {
    const std::string name = member_name_of(member);
    std::string base = without_extension(name);
    if (config::keep_source_extensions()) {
        base += "." + extension_of(name);
    }
    return container + "\\" + base + ".tags";
}

// One candidate source - a file from the folder, or a member of an archive. The
// exclusion list is the only test that stops one: the original queued everything
// else and left the question of whether it is audio to the read, which is why a
// .png is traced as queued and then yields nothing.
void consider_source(const std::string& file, bool member,
                     const std::string& destination_folder, const std::string& document,
                     std::vector<t_job>& jobs) {
    log_verbose("Checking %s", file.c_str());

    if (extension_excluded(file)) {
        log_verbose("File extension is excluded. Skipping %s", file.c_str());
        return;
    }
    // A document already sitting here is a source, not a file that is already
    // documented: it is the very file the tests below look for, so it goes to the
    // queue untested. The original queued it and let the writer decide.
    if (!member && !is_document_path(file)) {
        const std::string base =
            folder_of(file) + "\\" + without_extension(file_name_of(file));

        // The test asks about the document that would be written, in the folder the
        // documents go to - which is what makes "in separate folder" work at all: a
        // document in the source folder says nothing about the destination.
        // It applies in both modes: with "a separate file for each source" a
        // folder-wide document still makes every source in that folder skipped.
        //
        // The candidate document is put through the exclusion list too - that is
        // the original's extra "Checking exclusion: TAGS" line. A list that
        // excludes TAGS means existing documents are ignored and the folder built
        // again.
        if (!extension_excluded(folder_wide_file(destination_folder))
            && exists(folder_wide_file(destination_folder))) {
            // The destination already has this folder's document; rebuilding it
            // would only reproduce the same sources.
            log_verbose("Folder m-TAGS file exists. Skipping %s", file.c_str());
            return;
        }
        // Both candidates the original tested, under its one message.
        if (exists(per_source_file(destination_folder, file, ".tag"))
            || exists(per_source_file(destination_folder, file))) {
            log_verbose("m-TAGS file with same name exists. Skipping %s", file.c_str());
            return;
        }
        if (exists(base + ".cue")) {
            // A cue sheet describes this media already.
            log_verbose("CUE file with same name exists. Skipping %s", file.c_str());
            return;
        }
    }

    log_verbose("Adding audio file to queue: %s", file.c_str());
    job_for(jobs, document).sources.push_back(file);
}

// The folder scan. Every file is reported as it is considered, and the ones that
// cannot be sources say why. An archive is opened for its members unless "Do not
// parse archives" is set.
void plan_folder(const std::string& source_folder, const std::string& destination_folder,
                 std::vector<t_job>& jobs, abort_callback& abort, t_progress& progress) {
    pfc::list_t<pfc::string8> files;
    try {
        listFiles(source_folder.c_str(), files, abort);
    }
    catch (...) {
        log_error("Error while listing folder %s", source_folder.c_str());
        return;
    }

    // Ungated and once per walk, before any file is examined: the original printed
    // it here. It is the list as it is searched for - wrapped in the pipes, so an
    // empty one prints as "||".
    const std::string excluded =
        "|" + uppercase(config::excluded_extensions().get_ptr()) + "|";
    log_error("Excluded extensions: %s", excluded.c_str());

    for (t_size i = 0; i < files.get_count(); ++i) {
        const std::string file = files[i].get_ptr();
        progress.item(file);

        // What the original traced for every item it was handed, before examining
        // it: the path on its own, then the rewrite's input and output - the same
        // path under the default prefix configuration.
        const std::string url = canonical_path(file.c_str());
        log_verbose("%s", url.c_str());
        log_verbose("FROM: %s", url.c_str());
        log_verbose("TO: %s", url.c_str());

        // An archive with parsing allowed is a container, not a candidate: the
        // original never tested it, it walked its members into a document of their
        // An archive with parsing allowed is a container, not a candidate: the
        // original never tested it, it walked its members into a document of their
        // own - named after the archive - and left the folder-wide document to the
        // loose files.
        if (!config::do_not_parse_archives() && is_archive_file(file)) {
            const std::string container = archive_container(destination_folder, file);
            for (const std::string& member : list_archive(file, abort)) {
                progress.item(member);
                const std::string document =
                    config::separate_file_per_source() ? member_document(container, member)
                                                       : container;
                log_verbose("%s", member.c_str());
                log_verbose("FROM: %s", member.c_str());
                // The member's destination is that path, which is what the original
                // printed in the "TO" line's place.
                log_verbose("TO: %s",
                            canonical_path(
                                (container + "\\" + member_name_of(member)).c_str()).c_str());
                consider_source(member, true, destination_folder, document, jobs);
            }
            continue;
        }

        consider_source(file, false, destination_folder,
                        source_document(destination_folder, file), jobs);
    }
}

// The selection path. Each selected item is already a subsong of its media, so
// there is nothing to split - the original walked the selection and logged
// "Number of tracks" once, then each entry.
void write_selection_document(metadb_handle_list_cref items, const std::string& destination,
                              abort_callback& abort, t_progress& progress) {
    log_error("Number of tracks: %s", pfc::format_uint(items.get_count()).get_ptr());

    const std::string document = canonical_path(destination.c_str());

    t_document doc;
    for (t_size i = 0; i < items.get_count(); ++i) {
        const char* path = items[i]->get_path();
        const unsigned index = items[i]->get_subsong_index();

        progress.item(path);
        progress.step(i + 1, items.get_count());

        // A selection can be dragged in by hand, so it is put to the same question a
        // scanned folder is.
        if (!is_audio_file(path)) {
            log_verbose("Not an audio file");
            continue;
        }

        std::string media = path;
        if (index != 0) {
            media += "|" + std::string(pfc::format_uint(index).get_ptr());
        }
        // Verified against the original's own log (2026-10-06): the second field is
        // the source's native path, not an empty string - the member index only takes
        // its place when there is one.
        log_verbose("Processing %s | %s", path,
                    index == 0 ? native_path(path).c_str()
                               : pfc::format_uint(index).get_ptr());

        // The locator is rewritten against the document's own location.
        const std::string locator = locator_for(document, media);

        // "Ignore source tags" leaves the locator and nothing else, exactly as it
        // does for a folder - both paths retrieve metadata through one helper.
        if (config::ignore_source_tags()) {
            doc.add_track(bare_track(locator));
        }
        else {
            file_info_impl info;
            log_verbose("Retrieving metadata");
            if (items[i]->get_info(info)) {
                doc.add_track(build_track(info, locator));
            }
            log_verbose("Retrieved metadata");
        }
    }

    log_verbose("Writing m-TAGS file: %s", canonical_path(destination.c_str()).c_str());
    write_document(destination.c_str(), doc, abort);
}

// The three captions the original's progress dialogs carried. They are window
// titles, not trace lines: one file from a selection, many files from a folder,
// and the write-back.
const char* const caption_create_file = "Creating m-TAGS file...";
const char* const caption_create = "Creating m-TAGS files...";
const char* const caption_rewrite = "Rewriting media tags...";

// A single document from a selection still gets its own dialog, because that is
// what the original showed for the command.
void run_selection_creation(metadb_handle_list items, std::string destination) {
    threaded_process::g_run_modeless(
        threaded_process_callback_lambda::create(
            [items, destination](threaded_process_status& status, abort_callback& abort) {
                t_progress progress{ &status };
                write_selection_document(items, destination, abort, progress);
            }),
        threaded_process::flag_show_progress | threaded_process::flag_show_abort
            | threaded_process::flag_show_item | threaded_process::flag_show_delayed,
        core_api::get_main_window(), caption_create_file);
}

// The creators do the same work once their folders are known: plan, then write.
// Both halves run on a worker thread, with the dialog as their progress meter and
// their abort button.
void run_creation(std::vector<t_folder_job> folders) {
    threaded_process::g_run_modeless(
        threaded_process_callback_lambda::create(
            [folders = std::move(folders)](
                threaded_process_status& status, abort_callback& abort) {
                t_progress progress{ &status };

                std::vector<t_job> jobs;
                for (const t_folder_job& folder : folders) {
                    // The pair the original logged per folder argument: the source
                    // and the destination folder it was mapped to, in the "file://"
                    // form it worked in. The two are the same folder unless "in
                    // separate folder" was chosen.
                    log_verbose("FROM: %s", canonical_path(folder.source.c_str()).c_str());
                    log_verbose("TO: %s", canonical_path(folder.destination.c_str()).c_str());
                    plan_folder(folder.source, folder.destination, jobs, abort, progress);
                }

                t_size total = 0;
                t_size done = 0;
                for (const t_job& job : jobs) {
                    total += job.sources.size();
                }
                for (const t_job& job : jobs) {
                    if (job.sources.empty()) {
                        continue;
                    }
                    write_job(job.destination, job.sources, abort, progress);
                    done += job.sources.size();
                    progress.step(done, total);
                }
            }),
        threaded_process::flag_show_progress | threaded_process::flag_show_abort
            | threaded_process::flag_show_item | threaded_process::flag_show_delayed,
        core_api::get_main_window(), caption_create);
}

} // namespace

void create_from_selection(metadb_handle_list_cref items) {
    if (items.get_count() == 0) {
        return;
    }

    // Default destination: the first source's folder, named after the folder-wide
    // setting. The original computed the same default and then let it be changed.
    const std::string folder = folder_for(items[0]->get_path());
    pfc::string8 destination = folder_wide_file(folder).c_str();

    if (!fb2k::getOpenFileName(nullptr, "m-Tags files|*.tags|All files|*.*", 0, "tags",
                               "Destination file", folder.c_str(), destination, TRUE)) {
        // The original's dialog helper logged this through FUN_100264e0 before
        // handing back an empty path.
        log_verbose("Canceled");
        return;
    }
    log_verbose("Selected file: %s", destination.get_ptr());
    log_verbose("TO: %s", destination.get_ptr());

    run_selection_creation(metadb_handle_list(items), destination.get_ptr());
}

namespace {

// One folder-wide job: the source folder, plus the folder its document goes in. For
// the separate command that is the destination itself - VERIFIED against the original
// (2026-10-06): source `…\src` and destination `…\dest` put the document at
// `…\dest\!.tags`, with `TO: …\dest` in its log. The source folder's name is NOT
// mirrored; the name the original appends (FUN_10020390) is the per-source document
// name used by separate-file mode. The File menu's two commands share this.
bool add_folder_job(std::vector<t_folder_job>& jobs, const std::string& source,
                    const std::string& destination, bool same_folder) {
    jobs.push_back(t_folder_job{
        source, same_folder ? source : without_trailing_separator(native_path(destination.c_str())) });
    return true;
}

} // namespace

void create_folder_wide_browse(bool same_folder) {
    // The two File menu commands. A menu command has no selection to work from, so
    // the folder is asked for. Both titles are the original's own - "Source path"
    // and "Destination folder" - as is its save-file dialog's "Destination file".
    pfc::string8 source;
    if (!fb2k::browseForFolder(nullptr, "Source path", source)) {
        log_verbose("Canceled");
        return;
    }
    log_verbose("Selected folder: %s", source.get_ptr());
    if (!filesystem::g_is_valid_directory(source.get_ptr(), fb2k::noAbort)) {
        log_error("Invalid folder selected");
        return;
    }

    const std::string folder = native_path(source.get_ptr());
    std::vector<t_folder_job> jobs;

    if (!same_folder) {
        pfc::string8 destination = source;
        if (!fb2k::browseForFolder(nullptr, "Destination folder", destination)) {
            log_verbose("Canceled");
            return;
        }
        log_verbose("Selected folder: %s", destination.get_ptr());
        if (!filesystem::g_is_valid_directory(destination.get_ptr(), fb2k::noAbort)) {
            log_error("Invalid folder selected");
            return;
        }
        if (!add_folder_job(jobs, folder, destination.get_ptr(), false)) {
            return;
        }
    }
    else {
        // The document goes into the folder that holds the sources.
        add_folder_job(jobs, folder, folder, true);
    }

    run_creation(std::move(jobs));
}

void create_folder_wide_for(const char* folder) {
    const std::string source = native_path(folder);
    if (source.empty()
        || !filesystem::g_is_valid_directory(source.c_str(), fb2k::noAbort)) {
        log_error("Not a folder: %s. No action taken.", folder);
        return;
    }

    run_creation(std::vector<t_folder_job>{ t_folder_job{ source, source } });
}

void write_tags_to_media(metadb_handle_list_cref items) {
    log_error("Number of entries: %s", pfc::format_uint(items.get_count()).get_ptr());

    // The rewritten entries are collected during the worker-thread pass and sent
    // to the database once the dialog is gone - the refresh is a main-thread job.
    auto refresh = std::make_shared<metadb_handle_list>();
    metadb_handle_list handles(items);

    threaded_process::g_run_modeless(
        threaded_process_callback_lambda::create(
            [](auto) {},
            [handles, refresh](threaded_process_status& status, abort_callback& abort) {
                for (t_size i = 0; i < handles.get_count(); ++i) {
                    const char* path = handles[i]->get_path();
                    // One selected entry is one track of a document, and the
                    // write-back rewrites the media of the entries it was given, not
                    // of the whole document: the original walked the selection and
                    // wrote each entry's own referenced file. That is what keeps
                    // selecting one track from rewriting an album - and what keeps a
                    // 300-track album from meaning 90000 writes.
                    const unsigned subsong = handles[i]->get_subsong_index();
                    status.set_item(path);
                    status.set_progress(i, handles.get_count());

                    if (!is_document_path(path)) {
                        log_verbose("Skipping non m-TAGS source: %s", path);
                        continue;
                    }

                    t_document doc;
                    try {
                        doc = t_document::load(path);
                    }
                    catch (...) {
                        continue;
                    }
                    // 0 is "no subsong named", which is the first entry - the same
                    // reading the rest of the plugin gives it.
                    const unsigned selected = subsong == 0 ? 1 : subsong;
                    if (selected > doc.get_count()) {
                        log_verbose("Invalid subsong index: %s",
                                  pfc::format_uint(subsong).get_ptr());
                        continue;
                    }

                    const t_track& track = doc[selected - 1];

                    t_resolved media;
                    try {
                        media = resolve(path, track.locator);
                    }
                    catch (...) {
                        continue;
                    }
                    if (media.path.empty() || media.media != t_media::local) {
                        log_verbose("Skipping remote or unrecognized media: %s",
                                    media.path.c_str());
                        continue;
                    }
                    // A document may name another document. Writing this one's tags
                    // into that would fill a document with tags it does not hold.
                    if (is_document_path(media.path)) {
                        log_verbose("Skipping m-TAGS destination: %s",
                                    media.path.c_str());
                        continue;
                    }
                    log_verbose("Processing: %s [%s]", media.path.c_str(),
                                pfc::format_uint(media.index).get_ptr());

                    // The artwork goes first, and that order is not cosmetic: its commit
                    // rewrites the file's tag chunk area, and fb2k's WAV writer then lays
                    // a pre-existing "id3 " chunk out differently - JUNK padding with no
                    // LIST/INFO where the original's output carries an empty one.
                    if (!config::do_not_encapsulate_album_art()) {
                        try {
                            encapsulate_album_art(path, media.path.c_str(), abort);
                        }
                        catch (...) {
                            // Reported by the art layer itself; the tags are still written.
                        }
                    }

                    file_info_impl info;
                    apply_tags_to_media(track.tags, info);

                    try {
                        input_info_writer::ptr writer;
                        input_entry::g_open_for_info_write(writer, nullptr,
                                                           media.path.c_str(), abort);
                        // With the art step suppressed nothing else touches the media's
                        // artwork, and fb2k's own writers keep existing embedded art when
                        // the applied info carries none. The original's output has it
                        // dropped, so the tags are cleared first and the info applied on
                        // top of that.
                        if (config::do_not_encapsulate_album_art()) {
                            input_info_writer_v2::ptr v2;
                            if (writer->service_query_t(v2)) {
                                v2->remove_tags(abort);
                            }
                        }
                        writer->set_info(media.index, info, abort);
                        writer->commit(abort);
                    }
                    catch (...) {
                        // A file that cannot be written is reported by the input
                        // layer itself; the remaining entries are still worth
                        // attempting.
                        continue;
                    }

                    refresh->add_item(
                        metadb::get()->handle_create(media.path.c_str(), media.index));
                    log_verbose("Adding handle to refresh list: %s [%s]",
                                media.path.c_str(),
                                pfc::format_uint(media.index).get_ptr());
                }
            },
            [refresh](auto, bool) {
                if (refresh->get_count() > 0) {
                    metadb_io::get()->dispatch_refresh(*refresh);
                }
            }),
        threaded_process::flag_show_progress | threaded_process::flag_show_abort
            | threaded_process::flag_show_item | threaded_process::flag_show_delayed,
        core_api::get_main_window(), caption_rewrite);
}

} // namespace mtags
