// Advanced Preferences entries and the storage behind mtags::config.
//
// The GUIDs below are the original plugin's own, read out of its .rdata, and the
// tree shape, orders and defaults match it too. That is what makes this build
// config-compatible: fb2k keys every entry by its GUID, so an existing
// foo_tags.dll.cfg loads unchanged rather than appearing as a second set of
// settings. Verified against the original's cfg dump - the entry
// "Excluded extensions" really is 838DB58D-6822-485C-BC1B-D97DC1870ECB.

#include "config.h"

namespace mtags {
namespace config {

namespace {

// Branches. The root hangs off the SDK's standard "Tagging" branch, which is why
// the entries appear under Advanced Preferences > Tagging > m-TAGS.
const GUID guid_branch_root    = { 0xa6ffe5b1, 0x3cab, 0x4bee, { 0xb3, 0x52, 0x66, 0x29, 0x96, 0x17, 0x0a, 0xe4 } };
const GUID guid_branch_creator = { 0x1be27dff, 0xf263, 0x4aec, { 0xb0, 0x79, 0x88, 0x07, 0x2f, 0xc0, 0x29, 0x75 } };
const GUID guid_branch_prefix  = { 0xff6e864d, 0x09bc, 0x4e57, { 0x88, 0xc9, 0x6e, 0xf8, 0xf0, 0x01, 0x68, 0x13 } };

// Checkboxes.
const GUID guid_verbose_logging    = { 0x5c4e40ed, 0xa3a3, 0x4617, { 0x99, 0xcd, 0x34, 0xe8, 0x0f, 0xa8, 0x70, 0x51 } };
const GUID guid_no_album_art_embed = { 0x4e19520a, 0x7d34, 0x461e, { 0x8a, 0xed, 0x6e, 0x35, 0x61, 0x88, 0x20, 0x44 } };
const GUID guid_all_tags           = { 0x2c6f3732, 0xe65d, 0x4754, { 0xa6, 0x5f, 0xea, 0x8a, 0xce, 0x81, 0xc6, 0x9a } };
const GUID guid_keep_duration      = { 0x8a2ce0f5, 0x2c32, 0x40fe, { 0xbb, 0x78, 0x3e, 0x4c, 0x5b, 0xf4, 0xaa, 0xaf } };
const GUID guid_ignore_source_tags = { 0x9a51697f, 0xf959, 0x4876, { 0x8a, 0xf1, 0x83, 0x99, 0x21, 0x23, 0x6a, 0x92 } };
const GUID guid_no_parse_archives  = { 0xb3a7d56f, 0x2670, 0x4f4a, { 0xb2, 0x9c, 0xda, 0xf2, 0x2f, 0x68, 0x7b, 0xc5 } };
const GUID guid_keep_src_ext       = { 0xc1056029, 0x8883, 0x447c, { 0x99, 0x48, 0xf1, 0xe6, 0x1f, 0x08, 0xcb, 0xfb } };
const GUID guid_overwrite          = { 0x8a892c99, 0xe0af, 0x4131, { 0x99, 0xc4, 0x1d, 0x38, 0x32, 0x23, 0xee, 0x53 } };
const GUID guid_separate_file      = { 0xb056cec7, 0x30af, 0x464a, { 0x8e, 0x2e, 0x8a, 0xde, 0xb7, 0x64, 0x9c, 0xfc } };
const GUID guid_absolute_paths     = { 0x5f864e03, 0x0bd5, 0x4e44, { 0x96, 0x86, 0xab, 0x1c, 0xbc, 0x55, 0xbe, 0xe0 } };

// Strings.
const GUID guid_folder_wide_name   = { 0xdeb80da4, 0xca43, 0x4201, { 0xb8, 0x4a, 0x31, 0x9c, 0x20, 0x22, 0x7f, 0xc5 } };
const GUID guid_excluded_ext       = { 0x838db58d, 0x6822, 0x485c, { 0xbc, 0x1b, 0xd9, 0x7d, 0xc1, 0x87, 0x0e, 0xcb } };
const GUID guid_local_prefixes     = { 0x776e9f19, 0x7552, 0x4218, { 0x8a, 0x78, 0xd3, 0x5a, 0x58, 0xe6, 0xbe, 0x0a } };
const GUID guid_remote_prefixes    = { 0x8bb42256, 0x16f7, 0x418b, { 0xb2, 0x91, 0xae, 0xf1, 0x9a, 0xb4, 0xc3, 0x84 } };

// Orders are the original's: 6 on the root, 14 on the two sub-branches, 0 on
// every entry, so each branch sorts after the entries beside it.
advconfig_branch_factory   g_branch_root("m-TAGS", guid_branch_root, advconfig_entry::guid_branch_tagging, 6);
advconfig_branch_factory   g_branch_creator("m-TAGS creator", guid_branch_creator, guid_branch_root, 14);
advconfig_branch_factory   g_branch_prefix("Media prefixes", guid_branch_prefix, guid_branch_root, 14);

advconfig_checkbox_factory g_verbose_logging("Enable verbose logging", guid_verbose_logging, guid_branch_root, 0, false);
advconfig_checkbox_factory g_no_album_art_embed("Do not encapsulate album art", guid_no_album_art_embed, guid_branch_root, 0, false);
advconfig_checkbox_factory g_all_tags("Always write all tags for each source", guid_all_tags, guid_branch_root, 0, false);
advconfig_checkbox_factory g_keep_duration("Do not overwrite duration tag", guid_keep_duration, guid_branch_root, 0, false);

advconfig_string_factory   g_folder_wide_name("Folder-wide m-TAGS file name", guid_folder_wide_name, guid_branch_creator, 0, "!");
advconfig_checkbox_factory g_ignore_source_tags("Ignore source tags", guid_ignore_source_tags, guid_branch_creator, 0, false);
advconfig_checkbox_factory g_no_parse_archives("Do not parse archives", guid_no_parse_archives, guid_branch_creator, 0, false);
advconfig_string_factory   g_excluded_ext("Excluded extensions", guid_excluded_ext, guid_branch_creator, 0, "");
advconfig_checkbox_factory g_keep_src_ext("Keep source files extensions in non folder-wide m-TAGS file names", guid_keep_src_ext, guid_branch_creator, 0, false);
advconfig_checkbox_factory g_overwrite("Overwrite existing m-TAGS files", guid_overwrite, guid_branch_creator, 0, false);
advconfig_checkbox_factory g_separate_file("Create a separate m-TAGS file for each source file", guid_separate_file, guid_branch_creator, 0, false);
advconfig_checkbox_factory g_absolute_paths("Only use absolute paths in references", guid_absolute_paths, guid_branch_creator, 0, false);

advconfig_string_factory   g_local_prefixes("Local", guid_local_prefixes, guid_branch_prefix, 0, "file:|unpack:|cdda:");
advconfig_string_factory   g_remote_prefixes("Remote", guid_remote_prefixes, guid_branch_prefix, 0, "");

} // namespace

bool verbose_logging()           { return g_verbose_logging.get(); }
bool do_not_encapsulate_album_art() { return g_no_album_art_embed.get(); }
bool always_write_all_tags()     { return g_all_tags.get(); }
bool do_not_overwrite_duration() { return g_keep_duration.get(); }

pfc::string8 folder_wide_name()  { return g_folder_wide_name.get(); }
bool ignore_source_tags()        { return g_ignore_source_tags.get(); }
bool do_not_parse_archives()     { return g_no_parse_archives.get(); }
pfc::string8 excluded_extensions() { return g_excluded_ext.get(); }
bool keep_source_extensions()    { return g_keep_src_ext.get(); }
bool overwrite_existing()        { return g_overwrite.get(); }
bool separate_file_per_source()  { return g_separate_file.get(); }
bool only_absolute_paths()       { return g_absolute_paths.get(); }

pfc::string8 local_prefixes()    { return g_local_prefixes.get(); }
pfc::string8 remote_prefixes()   { return g_remote_prefixes.get(); }

} // namespace config
} // namespace mtags
