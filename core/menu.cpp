// Menu registration: the File menu popup and the context-menu entries.
//
// The four command GUIDs are the original's, and so is which pair belongs where.
// The two re-sort commands are this plugin's own and carry fresh GUIDs - there was
// no such command in the original to inherit one from.
// Reading the GUID tables out of the original's data settles it, because each pair
// sits at the address its own service reads:
//
//   File > m-TAGS        0x10064fc0 / 0x10064fb0  {0bfdcfa1-...} / {2fbf6289-...}
//   [context] Tagging    0x10064014 / 0x10064024  {dfcd8570-...} / {b4a27a4d-...}
//
// The names agree with the addresses: the pair the original's get_command()
// returns (FUN_1001fd20) is the pair its get_description() names "Create m-TAGS
// (in same folder)" / "(in separate folder)" (FUN_1001fd70), and its execute for
// that pair (FUN_1001fbc0 -> FUN_10020d20 with no path) *browses for a source
// folder* - which only makes sense for a File menu command, because a context
// menu command always has a selection. The other pair is the context menu's, and
// the two long strings ("Create an m-TAGS file from selected sources" / "Write
// m-TAGS metadata back into media files") are its descriptions.
//
// Both providers text their two entries per index: the short name for the menu and
// the longer line for anything that asks for a description.
//
// The group GUID cannot be checked that way - foobar2000 never persists a menu
// group - but the original's own table of GUIDs carries it, and that is the one
// used below. The original registered it as a popup under mainmenu_groups::file,
// at the priority below.

#include <SDK/foobar2000.h>

#include "creator.h"
#include "log.h"
#include "sorting.h"

namespace {

using namespace mtags;

// The two File menu commands: the folder-wide pair.
const GUID guid_folder_same =
    { 0x0bfdcfa1, 0xe206, 0x40b2, { 0xbd, 0xb5, 0xe5, 0x29, 0x64, 0x61, 0x9d, 0x98 } };
const GUID guid_folder_separate =
    { 0x2fbf6289, 0xb805, 0x4abe, { 0xa6, 0xe4, 0x68, 0xb6, 0x6f, 0x8d, 0xf7, 0x17 } };

// The two context menu items: the selection pair.
const GUID guid_create_file =
    { 0xdfcd8570, 0x6567, 0x4b07, { 0xaa, 0x50, 0x35, 0x7e, 0xfd, 0xec, 0xf8, 0x0b } };
const GUID guid_write_to_media =
    { 0xb4a27a4d, 0x2dd1, 0x46bc, { 0x86, 0xd6, 0xd6, 0x76, 0xc6, 0x0f, 0x1e, 0xd7 } };

// The two re-sort commands, new here.
const GUID guid_resort_track_number =
    { 0x6d434d87, 0x696d, 0x48a9, { 0x9a, 0x58, 0x52, 0x2e, 0xb1, 0x8e, 0xde, 0xab } };
const GUID guid_resort_playlist_order =
    { 0x59c0afd4, 0xa814, 0x44a7, { 0xa2, 0x67, 0x3d, 0x77, 0xb1, 0xfc, 0xc0, 0x56 } };

// The group GUID is the original's too, read from the GUID table at 0x10064FA0 that
// sits directly above the two folder-wide GUIDs. (foobar2000 never persists a
// group, so this one has to come from the binary or be invented - it is in the
// binary.) The original registered it as a popup under mainmenu_groups::file, at
// the priority below.
const GUID guid_group_file =
    { 0x9e624a2f, 0x9b64, 0x4246, { 0x97, 0xff, 0x8d, 0x25, 0x57, 0x8b, 0xf6, 0x8d } };

// The File menu's names, which the original also used as their descriptions.
const char* const folder_name[2] = { "Create m-TAGS (in same folder)",
                                     "Create m-TAGS (in separate folder)" };

// The context menu's names and descriptions.
const char* const name_create = "Create m-TAGS file";
const char* const name_write = "Write m-TAGS to media files";
const char* const desc_create = "Create an m-TAGS file from selected sources";
const char* const desc_write = "Write m-TAGS metadata back into media files";
const char* const name_resort_number = "Re-sort m-TAGS by track number";
const char* const name_resort_playlist = "Re-sort m-TAGS reflecting playlist order";
const char* const desc_resort_number =
    "Re-order the selected m-TAGS file's entries by track number";
const char* const desc_resort_playlist =
    "Re-order the selected m-TAGS file's entries to match the playlist";

// The original registered its popup at this priority, which is what draws it as
// its own separator block rather than glued to the top of the File menu.
const t_uint32 group_priority = 0x80000000;

// ---------------------------------------------------------------------------
// [main] File > m-TAGS

mainmenu_group_popup_factory g_group_file(guid_group_file, mainmenu_groups::file,
                                          group_priority, "m-TAGS");

class t_file_commands : public mainmenu_commands {
public:
    t_uint32 get_command_count() override { return 2; }

    GUID get_command(t_uint32 index) override {
        return index == 0 ? guid_folder_same : guid_folder_separate;
    }

    void get_name(t_uint32 index, pfc::string_base& out) override {
        out = folder_name[index == 0 ? 0 : 1];
    }

    bool get_description(t_uint32 index, pfc::string_base& out) override {
        // The original described these with their own names.
        out = folder_name[index == 0 ? 0 : 1];
        return true;
    }

    GUID get_parent() override { return guid_group_file; }

    t_uint32 get_sort_priority() override { return sort_priority_base + 0; }

    void execute(t_uint32 index, ctx_t) override {
        // No selection to work from: both commands ask for the folder.
        create_folder_wide_browse(index == 0);
    }
};

mainmenu_commands_factory_t<t_file_commands> g_file_commands;

// ---------------------------------------------------------------------------
// [context] Tagging
//
// Two items, both acting on the selection: "Create m-TAGS file" runs the
// selection-based creator (it asks for a file name) and "Write m-TAGS to media
// files" is the write-back. They are the pair whose names and long descriptions
// the original's contextmenu_item held, and they are the only ones it put in the
// context menu - the folder-wide pair is not offered there at all.

class t_context_commands : public contextmenu_item_simple {
public:
    unsigned get_num_items() override { return 4; }

    GUID get_item_guid(unsigned index) override {
        switch (index) {
        case 0: return guid_create_file;
        case 1: return guid_write_to_media;
        case 2: return guid_resort_track_number;
        default: return guid_resort_playlist_order;
        }
    }

    void get_item_name(unsigned index, pfc::string_base& out) override {
        switch (index) {
        case 0: out = name_create; break;
        case 1: out = name_write; break;
        case 2: out = name_resort_number; break;
        default: out = name_resort_playlist; break;
        }
    }

    // The original placed these under the Tagging group by GUID; it never carried the
    // literal. The group is what the console's prefs list reads, and the default path
    // string is not needed once the group is given.
    GUID get_parent() override {
        return contextmenu_groups::tagging;
    }

    bool get_item_description(unsigned index, pfc::string_base& out) override {
        switch (index) {
        case 0: out = desc_create; break;
        case 1: out = desc_write; break;
        case 2: out = desc_resort_number; break;
        default: out = desc_resort_playlist; break;
        }
        return true;
    }

    // No get_enabled_state: both are offered, which is what the base class
    // (contextmenu_item_simple) answers with.

    void context_command(unsigned index, metadb_handle_list_cref items, const GUID&) override {
        switch (index) {
        case 0: create_from_selection(items); break;
        case 1: write_tags_to_media(items); break;
        case 2: resort_by_track_number(items); break;
        default: resort_by_playlist_order(items); break;
        }
    }
};

contextmenu_item_factory_t<t_context_commands> g_context_commands;

} // namespace
