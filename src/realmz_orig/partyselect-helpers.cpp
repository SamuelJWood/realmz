/* This file is not part of the original implementation; it was added in order
 * to eliminate the Data CD file. */

#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <phosg/Filesystem.hh>
#include <string>
#include <unordered_set>
#include <vector>

#include "../FileManager.hpp"
#include "convert.h"
#include "structs.h"

static std::vector<std::pair<std::string, short>> characters;
static std::unordered_set<std::string> hidden_characters;

extern "C" void hide_character_from_list(const char* name) {
  hidden_characters.emplace(name);
}

extern "C" void unhide_character_from_list(const char* name) {
  hidden_characters.erase(name);
}

extern "C" void update_character_files_list() {
  characters.clear();
  // mac_list_directory merges the user's data folder with the bundled one, so
  // any character files the player drops into their Character Files folder are
  // picked up automatically (no Import step needed).
  for (const auto& filename : mac_list_directory(":Character Files")) {
    auto f = mac_fopen_unique(std::format(":Character Files:{}", filename), "rb");
    // Skip anything that isn't a valid character file. A real character file is
    // written as a single struct character (see misc.c), so it is exactly
    // sizeof(struct character) bytes. This filters out subdirectories (which
    // open as null), OS metadata (.DS_Store, Thumbs.db, Icon\r), the legacy
    // "Data CD" index, and any other stray file — so they neither crash the
    // read below nor appear as garbage entries in the list.
    if (!f) {
      continue;
    }
    if (fseek(f.get(), 0, SEEK_END) != 0 ||
        ftell(f.get()) != static_cast<long>(sizeof(struct character))) {
      continue;
    }
    rewind(f.get());
    try {
      auto ch = phosg::freadx<struct character>(f.get());
      CvtCharacterToPc(&ch);
      characters.emplace_back(filename, ch.level);
    } catch (const std::exception&) {
      continue;
    }
  }
  std::sort(characters.begin(), characters.end());
}

extern "C" uint32_t get_character_list_count() {
  return characters.size();
}

extern "C" void get_character_info_from_list(uint32_t index, const char** name, short* level) {
  try {
    const auto& ch = characters.at(index);
    if (!hidden_characters.count(ch.first)) {
      *name = ch.first.c_str();
      *level = ch.second;
      return;
    }
  } catch (const std::out_of_range&) {
  }
  *name = nullptr;
  *level = 0;
}
