#pragma once

// The four menu commands.
//
// "Create an m-TAGS file from selected sources" writes one document holding every
// selected track, to a path the user picks. The two File menu commands build a
// folder-wide document from a folder the user picks - in that folder, or in a
// destination folder they pick next (the document goes straight into it; nothing is
// mirrored). "Write m-TAGS metadata back into media files"
// is the reverse: it pushes the selected entries' tags into the media they point
// at.

#include <SDK/foobar2000.h>

namespace mtags {

// [context] Tagging > Create m-TAGS file
void create_from_selection(metadb_handle_list_cref items);

// [main] File > m-TAGS > Create m-TAGS (in same folder) / (in separate folder).
// Both ask for the folder to start from, because a menu command has no selection.
void create_folder_wide_browse(bool same_folder);

// The /M-TAGS command line: one folder-wide document inside the named folder,
// with no dialogs and no picker - which is why the original called the folder-wide
// creator directly with "same folder" set. One path, one document, in that folder.
void create_folder_wide_for(const char* folder);

// [context] Tagging > Write m-TAGS to media files
void write_tags_to_media(metadb_handle_list_cref items);

} // namespace mtags
