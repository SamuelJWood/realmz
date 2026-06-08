#pragma once

#include <string>

// Minimal decompressor/extractor for classic StuffIt ("SIT!") archives.
//
// Supports the compression methods used by Realmz 3rd party scenario archives:
//   - method 0  (stored)
//   - method 13 (LZSS + Huffman, both dynamic and the 5 predefined static tables)
//   - folder markers (methods 32/33)
//
// Each archived file is written into the output tree with its data fork as
// "<name>" and its resource fork (if any) as "<name>.rsrc", matching the
// resource-fork convention used elsewhere in this project. Control characters
// in names (e.g. the carriage return in the "Icon\r" custom-icon file) are
// stripped so the names are valid on all host filesystems (notably Windows).
namespace stuffit {

// Returns the name of the top-level folder inside the archive without
// extracting anything, or "" if the archive can't be read/parsed.
std::string root_folder_name(const std::string& sit_host_path);

// Extracts the archive at sit_host_path into out_parent_dir (a host path).
// The archive's own top-level folder is created inside out_parent_dir.
// Returns true on success.
bool extract(const std::string& sit_host_path, const std::string& out_parent_dir);

} // namespace stuffit
