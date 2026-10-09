// The two re-sort commands.
//
// The document is parsed and re-emitted through the same writer the creator uses,
// so the compact form is re-derived rather than patched by hand: a tag equal to the
// previous set's is dropped again, and a tag that disappears is written "[]" again.
// That is the whole reason this is trivial here and awkward from outside, where the
// file has to be taken apart as text.
//
// Only the selected entries move, and only among themselves: their slots keep their
// places and take the selected entries in the new order. A selection that covers one
// entry, or a document already in the requested order, therefore changes nothing and
// nothing is written.

#include "sorting.h"

#include "format.h"
#include "log.h"
#include "text.h"
#include "writer.h"

#include <SDK/core_api.h>
#include <SDK/playlist.h>
#include <SDK/threaded_process.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace mtags {

namespace {

// The caption on the modeless dialog these commands run behind.
const char* const caption_resort = "Re-sorting m-TAGS files...";

// What decides the new order.
enum class t_kind { track_number, selection };

// The entries of one document the selection covers, in selection order - which is
// the playlist order the second command reproduces.
struct t_group {
    std::string path;
    std::vector<unsigned> subsongs;
    std::vector<t_size> handles;   // indices into the selection, parallel to subsongs
};

// One item a re-sort re-labelled: the handle the playlist holds, and the entry that
// handle now stands for. Playlist calls are the main thread's, so these are collected
// on the worker and handed over.
struct t_item {
    metadb_handle_ptr handle;
    unsigned entry;   // 1-based, as the document counts entries
};

t_group& group_for(std::vector<t_group>& groups, const char* path) {
    for (t_group& group : groups) {
        // One document, however the database spelled the path it was asked about.
        if (pfc::stricmp_ascii(group.path.c_str(), path) == 0) {
            return group;
        }
    }
    groups.push_back(t_group{ path, {}, {} });
    return groups.back();
}

// The number in a tag, or 0 for absent: "3", "03" and "3/12" all read as 3, and a
// value that does not start with a digit reads as absent.
long number_of(const t_tag_set& tags, const char* name) {
    const auto found = tags.find(name);
    if (found == tags.end() || found->second.values.empty()) {
        return 0;
    }
    const char* text = found->second.values.front().c_str();
    while (*text == ' ' || *text == '\t') {
        ++text;
    }
    if (*text < '0' || *text > '9') {
        return 0;
    }
    return std::strtol(text, nullptr, 10);
}

// Disc number first, then track number, so a multi-disc album's repeated track
// numbers do not interleave. A set with no disc number is disc 1 - the common
// single-disc case - and an entry with no track number goes after the ones that have
// one, keeping the order it already had.
bool by_track_number(const t_track& a, const t_track& b) {
    const long disc_a = number_of(a.tags, "discnumber");
    const long disc_b = number_of(b.tags, "discnumber");
    if ((disc_a > 0 ? disc_a : 1) != (disc_b > 0 ? disc_b : 1)) {
        return (disc_a > 0 ? disc_a : 1) < (disc_b > 0 ? disc_b : 1);
    }
    const long track_a = number_of(a.tags, "tracknumber");
    const long track_b = number_of(b.tags, "tracknumber");
    if (track_a == 0 || track_b == 0) {
        return track_a != 0;
    }
    return track_a < track_b;
}

// Lays the selected entries into `slots`, which are the document positions they
// already occupy. False when that leaves the document exactly as it was, in which
// case there is nothing to write.
bool reorder(t_document& doc, const std::vector<unsigned>& slots, t_kind kind) {
    if (slots.size() < 2) {
        return false;
    }

    // The slots in the order their new contents come from: the selection's own order
    // for one command, the entries' track numbers for the other.
    std::vector<unsigned> sources = slots;
    if (kind == t_kind::track_number) {
        std::stable_sort(sources.begin(), sources.end(), [&doc](unsigned a, unsigned b) {
            return by_track_number(doc[a], doc[b]);
        });
    }

    // The same slots top to bottom, which is where the new contents go.
    std::vector<unsigned> places = slots;
    std::stable_sort(places.begin(), places.end());

    bool moved = false;
    for (size_t i = 0; i < places.size(); ++i) {
        if (places[i] != sources[i]) {
            moved = true;
            break;
        }
    }
    if (!moved) {
        return false;
    }

    // Copied out first: the sources and the destinations are the same document.
    std::vector<t_track> fresh;
    fresh.reserve(sources.size());
    for (unsigned index : sources) {
        fresh.push_back(doc[index]);
    }
    for (size_t i = 0; i < places.size(); ++i) {
        doc.track_at(places[i]) = std::move(fresh[i]);
    }
    return true;
}

// Puts the items a re-sort re-labelled back into the file's order, inside the slots they
// already hold - the rest of the playlist is left exactly as it was. That is what a
// fresh drag-in of the same file shows; a playlist that skips it displays the file's
// entries in its own old order, because an item stands for an entry *position*: after a
// permute, the item reading "entry 3" simply holds whatever entry 3 now holds.
//
// Gather permutation, the way the SDK builds its own: order[new position] is the old
// index that belongs there.
void reorder_playlists(const std::vector<t_item>& items) {
    if (items.empty()) {
        return;
    }

    playlist_manager::ptr api = playlist_manager::get();
    const t_size playlists = api->get_playlist_count();

    for (t_size playlist = 0; playlist < playlists; ++playlist) {
        const t_size count = api->playlist_get_item_count(playlist);
        if (count == 0) {
            continue;
        }

        // Where the affected items sit, top to bottom, with the entry each stands for.
        struct t_spot {
            t_size index;
            unsigned entry;
            const char* path;
        };
        std::vector<t_spot> spots;
        for (t_size index = 0; index < count; ++index) {
            metadb_handle_ptr item;
            if (!api->playlist_get_item_handle(item, playlist, index)) {
                continue;
            }
            // Handles are shared, so equal ones are the same object - which is what the
            // SDK's own duplicate finder assumes too.
            for (const t_item& affected : items) {
                if (affected.handle == item) {
                    spots.push_back(
                        t_spot{ index, affected.entry, affected.handle->get_path() });
                    break;
                }
            }
        }
        if (spots.size() < 2) {
            continue;
        }

        // The file's order is the order its entries were written in.
        std::vector<t_spot> wanted = spots;
        std::stable_sort(wanted.begin(), wanted.end(),
                         [](const t_spot& a, const t_spot& b) { return a.entry < b.entry; });

        std::vector<t_size> order(count);
        for (t_size i = 0; i < count; ++i) {
            order[i] = i;
        }
        bool changed = false;
        for (size_t i = 0; i < spots.size(); ++i) {
            order[spots[i].index] = wanted[i].index;
            if (wanted[i].index != spots[i].index) {
                changed = true;
            }
        }
        if (!changed) {
            continue;
        }

        // A locked playlist refuses, which is its prerogative: the file is written either
        // way, so this is reported rather than treated as a failure.
        log_verbose(api->playlist_reorder_items(playlist, order.data(), count)
                        ? "Reordering playlist items for %s"
                        : "Cannot reorder playlist items for %s",
                    spots.front().path);
    }
}

// The same shape as the write-back: group the selection by document, do the work on
// the worker thread, and send the rewritten documents to the database afterwards,
// because the refresh is a main-thread job.
void run_resort(metadb_handle_list items, t_kind kind) {
    // A playlist can hold the same entry twice, and two picks in one slot would ask
    // the document for two different orders.
    items.remove_duplicates();

    log_error("Number of entries: %s", pfc::format_uint(items.get_count()).get_ptr());

    auto refresh = std::make_shared<metadb_handle_list>();
    auto moved = std::make_shared<std::vector<t_item>>();

    threaded_process::g_run_modeless(
        threaded_process_callback_lambda::create(
            [](auto) {},
            [items, kind, refresh, moved](threaded_process_status& status, abort_callback& abort) {
                std::vector<t_group> groups;
                for (t_size i = 0; i < items.get_count(); ++i) {
                    const char* path = items[i]->get_path();
                    status.set_item(path);
                    status.set_progress(i, items.get_count());

                    if (!is_document_path(path)) {
                        log_verbose("Skipping non m-TAGS source: %s", path);
                        continue;
                    }
                    t_group& group = group_for(groups, path);
                    group.subsongs.push_back(items[i]->get_subsong_index());
                    group.handles.push_back(i);
                }

                for (const t_group& group : groups) {
                    log_error("Re-sorting: %s", group.path.c_str());

                    t_document doc;
                    try {
                        doc = t_document::load(group.path.c_str());
                    }
                    catch (...) {
                        continue;
                    }

                    // 0 is "no subsong named", which is the first entry - the same
                    // reading the rest of the plugin gives it.
                    std::vector<unsigned> slots;
                    bool usable = true;
                    for (unsigned subsong : group.subsongs) {
                        const unsigned selected = subsong == 0 ? 1 : subsong;
                        if (selected > doc.get_count()) {
                            log_verbose("Invalid subsong index: %s",
                                        pfc::format_uint(subsong).get_ptr());
                            usable = false;
                            break;
                        }
                        slots.push_back(selected - 1);
                    }
                    if (!usable) {
                        continue;
                    }

                    for (unsigned slot : slots) {
                        log_verbose("Processing: %s [%s]", doc[slot].locator.c_str(),
                                    pfc::format_uint(slot + 1).get_ptr());
                    }

                    if (!reorder(doc, slots, kind)) {
                        log_verbose("Order unchanged: %s", group.path.c_str());
                        continue;
                    }

                    try {
                        write_document(group.path.c_str(), doc, abort);
                    }
                    catch (...) {
                        continue;
                    }

                    // The file was rewritten, so the playlist is told to read it again: every
                    // entry, not only the ones the selection named. A partial re-sort moves text
                    // around the entries it did not touch, and the index 0 alias of the first
                    // entry is what an item added by path alone carries.
                    for (unsigned subsong = 0;
                         subsong <= static_cast<unsigned>(doc.get_count()); ++subsong) {
                        refresh->add_item(
                            metadb::get()->handle_create(group.path.c_str(), subsong));
                        log_verbose("Adding handle to refresh list: %s [%s]",
                                    group.path.c_str(),
                                    pfc::format_uint(subsong).get_ptr());
                    }

                    // The playlist's own order is wrong for a different reason now: an item
                    // stands for an entry position, so the items this rewrite re-labelled have
                    // to be put into the file's order as well, or the playlist keeps showing
                    // the file's entries in its own old order.
                    for (t_size handle : group.handles) {
                        const unsigned subsong = items[handle]->get_subsong_index();
                        moved->push_back(t_item{ items[handle], subsong == 0 ? 1u : subsong });
                    }
                }
            },
            [refresh, moved](auto, bool) {
                reorder_playlists(*moved);

                // A reload, not a repaint. dispatch_refresh only asks components to redraw
                // from the metadb's own copy, which still holds the tags as they were
                // before the rewrite - the file is re-read here instead, which is what the
                // manual "Reload info from file(s)" does, and the only thing that stops the
                // playlist showing what each entry used to hold.
                if (refresh->get_count() > 0) {
                    metadb_io_v2::ptr api;
                    if (metadb_io::get()->service_query_t(api)) {
                        api->load_info_async(*refresh, metadb_io::load_info_force,
                                             core_api::get_main_window(),
                                             metadb_io_v2::op_flag_no_errors
                                                 | metadb_io_v2::op_flag_delay_ui,
                                             nullptr);
                    }
                }
            }),
        threaded_process::flag_show_progress | threaded_process::flag_show_abort
            | threaded_process::flag_show_item | threaded_process::flag_show_delayed,
        core_api::get_main_window(), caption_resort);
}

} // namespace

void resort_by_track_number(metadb_handle_list_cref items) {
    run_resort(items, t_kind::track_number);
}

void resort_by_playlist_order(metadb_handle_list_cref items) {
    run_resort(items, t_kind::selection);
}

} // namespace mtags
