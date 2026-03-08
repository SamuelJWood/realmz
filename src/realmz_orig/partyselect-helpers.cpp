/* This file is not part of the original implementation; it was added in order
 * to eliminate the Data CD file. */

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
  for (const auto& filename : mac_list_directory(":Character Files")) {
    auto f = mac_fopen_unique(std::format(":Character Files:{}", filename), "rb");
    auto ch = phosg::freadx<struct character>(f.get());
    CvtCharacterToPc(&ch);
    characters.emplace_back(std::make_pair(filename, ch.level));
  }
}

extern "C" void get_character_info_from_list(unsigned long index, const char** name, short* level) {
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
