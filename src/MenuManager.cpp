#include "MenuManager.hpp"
#include "EventManager.h"
#include "FileManager.h"
#include "FileManager.hpp"
#include "MemoryManager.hpp"
#include "MenuController.h"
#include "MenuManager-C-Interface.h"
#include "ResourceManager.h"
#include "SDLMenuBar.hpp"
#include "StringConvert.hpp"
#include "WindowManager.hpp"
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <list>
#include <phosg/Strings.hh>
#include <resource_file/ResourceFile.hh>
#include <stdexcept>
#include <unordered_map>

using ResourceDASM::ResourceFile;

static phosg::PrefixedLogger mm_log("[MenuManager] ");

class MenuManager {
public:
  MenuManager() = default;
  ~MenuManager() = default;

  std::shared_ptr<Menu> get_menu(int16_t res_id) {
    if (!this->res_id_to_menu.contains(res_id)) {
      mm_log.info_f("Loading MENU:{} from resource forks", res_id);
      auto handle = GetResource(ResourceDASM::RESOURCE_TYPE_MENU, res_id);
      auto decoded_menu = ResourceFile::decode_MENU(*handle, GetHandleSize(handle));
      auto menu = std::make_shared<Menu>(Menu(decoded_menu));
      this->res_id_to_menu.emplace(res_id, menu);
      this->handle_to_menu.emplace(handle, menu);
      this->menu_id_to_handle.emplace(menu->menu_id, handle);
    }
    return this->res_id_to_menu.at(res_id);
  }

  std::shared_ptr<Menu> get_menu(Handle handle) {
    return this->handle_to_menu.at(handle);
  }

  void load_menu_list(Handle mbar_handle) {
    auto data = read_from_handle(mbar_handle);
    auto num_menus = data.get_u16b();
    auto menu_list = std::make_shared<MenuList>();
    for (int i = 0; i < num_menus; i++) {
      auto menu_res_id = data.get_s16b();
      menu_list->menus.emplace_back(this->get_menu(menu_res_id));
    }
    this->handle_to_menulist.emplace(mbar_handle, menu_list);
  }

  void set_menu_list(Handle mbar_handle) {
    this->cur_menu_list = this->handle_to_menulist.at(mbar_handle);
  }

  MenuHandle handle_by_menu_id(int16_t menu_id, bool currently_in_list) {
    if (this->menu_id_to_handle.contains(menu_id)) {
      return this->menu_id_to_handle.at(menu_id);
    } else {
      // This MENU has not been loaded yet, which means that it hasn't yet appeared
      // in a MBAR resource that we've loaded. For some reason, the game is still trying
      // to get a handle to the unloaded menu. To try to satisfy that, assume that the
      // resource id of the menu is the same as the given menu id, try loading the menu,
      // and return it.
      mm_log.warning_f("Attempted to get menu with ID {}, but it doesn't appear in current MBAR", menu_id);
      try {
        this->get_menu(menu_id);
        return this->menu_id_to_handle.at(menu_id);
      } catch (std::out_of_range&) {
        // The MENU resource just doesn't exist. It was probably deleted in this version
        // of the game.
        return NULL;
      }
    }
  }

  void register_new_menu(Handle handle, std::shared_ptr<Menu> menu) {
    this->res_id_to_menu.emplace(menu->menu_id, menu);
    this->handle_to_menu.emplace(handle, menu);
    this->menu_id_to_handle.emplace(menu->menu_id, handle);
  }

  void insert_submenu(MenuHandle handle) {
    auto menu = this->get_menu(handle);
    for (auto& existing : this->cur_menu_list->submenus) {
      if (existing->menu_id == menu->menu_id) {
        existing = menu;
        return;
      }
    }
    this->cur_menu_list->submenus.emplace_back(menu);
  }

  void sync(void) {
    if (this->cur_menu_list != nullptr) {
      SDLMenuBar::instance().sync(this->cur_menu_list);
      // MCSync no longer needed — SDLMenuBar handles all menu display
    }
  }

  std::shared_ptr<MenuList> get_current_menu_list() {
    return this->cur_menu_list;
  }

  void remove(int16_t menu_id) {
    auto menu_handle = this->menu_id_to_handle.find(menu_id)->second;
    auto menu = this->handle_to_menu.find(menu_handle)->second;

    this->handle_to_menu.erase(menu_handle);
    this->menu_id_to_handle.erase(menu_id);
    for (auto m = this->res_id_to_menu.begin(); m != this->res_id_to_menu.end(); m++) {
      if ((*m).second->menu_id == menu_id) {
        this->res_id_to_menu.erase(m);
        break;
      }
    }

    for (auto m = this->cur_menu_list->submenus.begin(); m != this->cur_menu_list->submenus.end(); m++) {
      if ((*m)->menu_id == menu_id) {
        this->cur_menu_list->submenus.erase(m);
        return;
      }
    }

    for (auto m = this->cur_menu_list->menus.begin(); m != this->cur_menu_list->menus.end(); m++) {
      if ((*m)->menu_id == menu_id) {
        this->cur_menu_list->menus.erase(m);
        return;
      }
    }
  }

  int32_t find_item_by_key_equivalent(char ch) const {
    // Returns the menu ID in the high word and item ID in the low word, or 0
    // if no such item was found
    if (!this->cur_menu_list) {
      return 0;
    }
    ch = toupper(ch);
    for (const auto& menu_set : {this->cur_menu_list->menus, this->cur_menu_list->submenus}) {
      for (const auto& menu : menu_set) {
        if (!menu->enabled) {
          continue;
        }
        for (size_t item_id = 0; item_id < menu->items.size(); item_id++) {
          const auto& item = menu->items[item_id];
          if (!item.enabled) {
            continue;
          }
          if (toupper(item.key_equivalent) == ch) {
            return (menu->menu_id << 16) | item_id;
          }
        }
      }
    }
    return 0;
  }

private:
  std::shared_ptr<MenuList> cur_menu_list;
  std::unordered_map<Handle, std::shared_ptr<MenuList>> handle_to_menulist;
  std::unordered_map<MenuHandle, std::shared_ptr<Menu>> handle_to_menu;
  std::unordered_map<int16_t, std::shared_ptr<Menu>> res_id_to_menu;
  std::unordered_map<int16_t, MenuHandle> menu_id_to_handle;
};

static MenuManager mm;

Handle GetNewMBar(int16_t menuBarID) {
  mm_log.info_f("Loading MBAR:{} from resource forks", menuBarID);
  auto handle = GetResource(ResourceDASM::RESOURCE_TYPE_MBAR, menuBarID);
  mm.load_menu_list(handle);
  return handle;
}

MenuHandle GetMenuHandle(int16_t menuID) {
  return mm.handle_by_menu_id(menuID, true);
}

MenuHandle GetMenu(int16_t resourceID) {
  auto menu = mm.get_menu(resourceID);
  return mm.handle_by_menu_id(menu->menu_id, false);
}

void SetMenuBar(Handle menuList) {
  mm.set_menu_list(menuList);
}

void InsertMenu(MenuHandle theMenu, int16_t beforeID) {
  if (beforeID != -1) {
    mm_log.error_f("Called InsertMenu on a non sub-menu");
    return;
  }

  mm.insert_submenu(theMenu);
}

void GetMenuItemText(MenuHandle theMenu, uint16_t item, Str255 itemString) {
  auto menu = mm.get_menu(theMenu);
  auto menu_item = menu->items[item - 1];
  pstr_for_string<256>(itemString, menu_item.name);
}

void DrawMenuBar() {
  mm.sync();
  WindowManager::instance().redraw_menu_bar_only();
}

void DeleteMenu(int16_t menuID) {
  mm.remove(menuID);
}

void SetMenuItemText(MenuHandle theMenu, uint16_t item, ConstStr255Param itemString) {
  auto menu = mm.get_menu(theMenu);
  if (item > menu->items.size()) {
    mm_log.info_f("Tried to set text of menu item {} on menu {} but it only has {} items", item, menu->title, menu->items.size());
    return;
  }
  menu->items.at(item - 1).name = string_for_pstr<256>(itemString);
  mm.sync();
}

int32_t MenuSelect(Point startPt) {
  int16_t menu_id = -startPt.v;
  int16_t item_id = -startPt.h;
  mm_log.info_f("Clicked menu {}, item {}", menu_id, item_id);
  return (static_cast<int32_t>(menu_id) << 16) + item_id;
}

void DisableItem(MenuHandle theMenu, uint16_t item) {
  auto menu = mm.get_menu(theMenu);
  if (item == 0) {
    menu->enabled = false;
  } else if (item <= menu->items.size()) {
    menu->items[item - 1].enabled = false;
  } else {
    mm_log.warning_f("Attempted to disable MENU:{} item {}, but it doesn't exist", menu->menu_id, item);
  }
  mm.sync();
}

void EnableItem(MenuHandle theMenu, uint16_t item) {
  auto menu = mm.get_menu(theMenu);
  if (item == 0) {
    menu->enabled = true;
  } else if (item <= menu->items.size()) {
    menu->items[item - 1].enabled = true;
  } else {
    mm_log.warning_f("Attempted to enable MENU:{} item {}, but it doesn't exist", menu->menu_id, item);
  }
  mm.sync();
}

void CheckItem(MenuHandle theMenu, uint16_t item, Boolean checked) {
  auto menu = mm.get_menu(theMenu);
  if (item > menu->items.size()) {
    mm_log.warning_f("Attempted to (un)check MENU:{} item {}, but it doesn't exist", menu->menu_id, item);
  } else {
    menu->items.at(item - 1).checked = checked;
  }
  mm.sync();
}

void SetItemIcon(MenuHandle theMenu, int16_t item, int16_t iconIndex) {
  auto menu = mm.get_menu(theMenu);
  if (item < 1 || item > static_cast<int16_t>(menu->items.size())) {
    return;
  }
  auto& menu_item = menu->items.at(item - 1);
  menu_item.icon_number = static_cast<uint8_t>(iconIndex);
  if (iconIndex != 0) {
    int16_t cicn_id = static_cast<uint8_t>(iconIndex) + 256;
    auto handle = GetResource(ResourceDASM::RESOURCE_TYPE_cicn, cicn_id);
    if (handle) {
      auto cicn = ResourceFile::decode_cicn(*handle, GetHandleSize(handle));
      menu_item.icon_image = std::make_shared<phosg::ImageRGBA8888N>(std::move(cicn.image));
    }
  } else {
    menu_item.icon_image = nullptr;
  }
  mm.sync();
}

void SetItemIconByCicnId(MenuHandle theMenu, int16_t item, int16_t cicnId) {
  auto menu = mm.get_menu(theMenu);
  if (item < 1 || item > static_cast<int16_t>(menu->items.size())) {
    return;
  }
  auto& menu_item = menu->items.at(item - 1);
  if (cicnId != 0) {
    auto handle = GetResource(ResourceDASM::RESOURCE_TYPE_cicn, cicnId);
    if (handle) {
      auto cicn = ResourceFile::decode_cicn(*handle, GetHandleSize(handle));
      menu_item.icon_image = std::make_shared<phosg::ImageRGBA8888N>(std::move(cicn.image));
    }
  } else {
    menu_item.icon_image = nullptr;
  }
}

void SetItemIconFromScenarioPng(MenuHandle theMenu, int16_t item, const char* scenario_name) {
  auto menu = mm.get_menu(theMenu);
  if (item < 1 || item > static_cast<int16_t>(menu->items.size())) {
    return;
  }
  auto& menu_item = menu->items.at(item - 1);

  // Find a .png file in the scenario's directory.
  std::string dir_mac = std::string(":Scenarios:") + scenario_name;
  auto files = mac_list_directory(dir_mac);
  std::string png_host;
  for (const auto& fname : files) {
    if (fname.size() > 4 &&
        fname.substr(fname.size() - 4) == ".png") {
      std::string mac_path = dir_mac + ":" + fname;
      png_host = host_filename_for_mac_filename(mac_path, false);
      break;
    }
  }

  if (png_host.empty()) {
    mm_log.warning_f("No PNG icon found for scenario '{}'", scenario_name);
    menu_item.icon_image = nullptr;
    return;
  }

  SDL_Surface* surf = IMG_Load(png_host.c_str());
  if (!surf) {
    mm_log.warning_f("Failed to load PNG '{}': {}", png_host, SDL_GetError());
    menu_item.icon_image = nullptr;
    return;
  }

  SDL_Surface* rgba = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_ARGB8888);
  SDL_DestroySurface(surf);
  if (!rgba) {
    mm_log.warning_f("Failed to convert PNG surface: {}", SDL_GetError());
    menu_item.icon_image = nullptr;
    return;
  }

  int w = rgba->w, h = rgba->h;
  auto img = std::make_shared<phosg::ImageRGBA8888N>(w, h);
  for (int y = 0; y < h; y++) {
    const uint32_t* row = reinterpret_cast<const uint32_t*>(
        static_cast<const uint8_t*>(rgba->pixels) + y * rgba->pitch);
    for (int x = 0; x < w; x++) {
      img->write(x, y, phosg::rgba8888_for_argb8888(row[x]));
    }
  }
  SDL_DestroySurface(rgba);

  menu_item.icon_image = std::move(img);
}

void AppendMenu(MenuHandle menu, ConstStr255Param data) {
  auto m = mm.get_menu(menu);
  // TODO: Parse menu item format string (Macintosh Toolbox Essentials, 3-65)
  auto s = string_for_pstr<256>(data);
  auto& item = m->items.emplace_back();
  item.name = s;
}

void AppendMenuCStr(MenuHandle menu, const char* data) {
  auto m = mm.get_menu(menu);
  auto& item = m->items.emplace_back();
  item.name = std::string(data);
}

// Ugh, have to use global variable for the callback to be able to modify it
static int32_t result;

void popupCallback(int16_t menuId, int16_t itemId) {
  result = (menuId << 16) | itemId;
}

// The PopUpMenuSelect function returns the menu ID of the chosen menu in the high-order word of its function
// result and the chosen menu item in the low-order word. (3-120 Menu Manager Reference)
int32_t PopUpMenuSelect(MenuHandle menu, int16_t top, int16_t left, int16_t popUpItem) {
  auto m = mm.get_menu(menu);

  auto sdl_window = WindowManager::instance().get_sdl_window();
  auto properties = SDL_GetWindowProperties(sdl_window.get());
  auto nsWindow = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);

  result = -1;
  MCCreatePopupMenu(nsWindow, m, mm.get_current_menu_list(), {top, left}, &popupCallback);

  // Wait for either an item to be selected and fire the callback to modify result, or for
  // the menu to be closed without a selection, which will fire the callback with 0 as the result.
  while (result == -1) {
    SDL_Delay(1);
  }

  return result;
}

int16_t CountMItems(MenuHandle theMenu) {
  auto m = mm.get_menu(theMenu);
  return m->items.size();
}

int32_t MenuKey(int16_t ch) {
  return 0;
}

MenuHandle Realmz_NewMenu(int16_t menuID, ConstStr255Param menuTitle) {
  auto menu = std::make_shared<Menu>();
  menu->menu_id = menuID;
  menu->title = string_for_pstr<256>(menuTitle);
  menu->enabled = true;
  Handle handle = NewHandle(0);
  mm.register_new_menu(handle, menu);
  return handle;
}

void SetItemMark(MenuHandle theMenu, int16_t item, int16_t markChar) {
  auto menu = mm.get_menu(theMenu);
  if (item < 1 || item > static_cast<int16_t>(menu->items.size())) {
    return;
  }
  menu->items.at(item - 1).checked = (markChar != 0);
  mm.sync();
}

void GetItemMark(MenuHandle theMenu, int16_t item, int16_t* markChar) {
  auto menu = mm.get_menu(theMenu);
  if (item < 1 || item > static_cast<int16_t>(menu->items.size())) {
    *markChar = 0;
    return;
  }
  *markChar = menu->items.at(item - 1).checked ? 19 : 0;
}

void SetItemStyle(MenuHandle theMenu, int16_t item, int16_t style) {
  auto menu = mm.get_menu(theMenu);
  if (item < 1 || item > static_cast<int16_t>(menu->items.size())) {
    return;
  }
  menu->items.at(item - 1).style_flags = static_cast<uint8_t>(style);
  mm.sync();
}

void SetItemDescription(MenuHandle theMenu, int16_t item, const char* description) {
  auto menu = mm.get_menu(theMenu);
  if (item < 1 || item > static_cast<int16_t>(menu->items.size())) {
    return;
  }
  menu->items.at(item - 1).description = description ? description : "";
}

// === Description file loader ===

static std::unordered_map<std::string, std::string> g_desc_cache;
static bool g_desc_cache_loaded = false;

static std::string desc_normalize_key(const std::string& name) {
  std::string key = name;
  if (!key.empty() && key.back() == ':')
    key.pop_back();
  std::transform(key.begin(), key.end(), key.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return key;
}

static bool desc_is_entry_name(const std::string& line) {
  if (line.empty())
    return false;
  // UTF-8 bullet U+2022 = \xE2\x80\xA2
  if (line.size() >= 3 &&
      static_cast<unsigned char>(line[0]) == 0xE2 &&
      static_cast<unsigned char>(line[1]) == 0x80 &&
      static_cast<unsigned char>(line[2]) == 0xA2)
    return false;
  if (std::isdigit(static_cast<unsigned char>(line[0])))
    return false;
  if (line.length() > 40)
    return false;
  std::string lower = line;
  std::transform(lower.begin(), lower.end(), lower.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  static const char* const skip_prefixes[] = {
      "typical", "note", "height", "life", "examples", nullptr};
  for (int i = 0; skip_prefixes[i]; i++) {
    if (lower.rfind(skip_prefixes[i], 0) == 0)
      return false;
  }
  return true;
}

static void load_descriptions_file() {
  FILE* f = mac_fopen(":Data Files:Caste and Race descriptions.txt", "r");
  if (!f) {
    mm_log.warning_f("Could not open Caste and Race descriptions.txt");
    return;
  }

  std::string current_key;
  std::string current_body;
  bool prev_blank = true;
  char buf[2048];

  auto flush = [&]() {
    if (!current_key.empty()) {
      while (!current_body.empty() && current_body.back() == '\n')
        current_body.pop_back();
      if (!current_body.empty())
        g_desc_cache.emplace(current_key, std::move(current_body));
      current_key.clear();
      current_body.clear();
    }
  };

  while (fgets(buf, sizeof(buf), f)) {
    std::string line(buf);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
      line.pop_back();
    bool is_blank = line.empty() ||
        std::all_of(line.begin(), line.end(),
            [](unsigned char c) { return static_cast<bool>(std::isspace(c)); });
    if (!is_blank && prev_blank && desc_is_entry_name(line)) {
      flush();
      current_key = desc_normalize_key(line);
    } else if (!current_key.empty()) {
      current_body += line + "\n";
    }
    prev_blank = is_blank;
  }
  flush();
  fclose(f);
}

const char* GetDescriptionFromFile(const char* item_name) {
  if (!g_desc_cache_loaded) {
    load_descriptions_file();
    g_desc_cache_loaded = true;
  }

  std::string key = desc_normalize_key(item_name);

  auto try_key = [&](const std::string& k) -> const std::string* {
    auto it = g_desc_cache.find(k);
    return (it != g_desc_cache.end()) ? &it->second : nullptr;
  };

  if (auto* r = try_key(key))
    return r->c_str();
  // plural +s (Orc → Orcs, Human → Humans)
  if (auto* r = try_key(key + "s"))
    return r->c_str();
  // plural +es
  if (auto* r = try_key(key + "es"))
    return r->c_str();
  // f → ves (Elf → Elves, Dwarf → Dwarves)
  if (!key.empty() && key.back() == 'f') {
    if (auto* r = try_key(key.substr(0, key.size() - 1) + "ves"))
      return r->c_str();
  }
  // man → men (Lizard Man → Lizard Men)
  if (key.size() >= 3 && key.substr(key.size() - 3) == "man") {
    if (auto* r = try_key(key.substr(0, key.size() - 3) + "men"))
      return r->c_str();
  }
  // space ↔ hyphen, with optional plural (Half Elf → Half-Elf, Half-Elfs; Half Orc → Half-Orc, Half-Orcs)
  for (char from : {' ', '-'}) {
    char to = (from == ' ') ? '-' : ' ';
    std::string alt = key;
    bool changed = false;
    for (auto& c : alt) {
      if (c == from) { c = to; changed = true; }
    }
    if (changed) {
      if (auto* r = try_key(alt)) return r->c_str();
      if (auto* r = try_key(alt + "s")) return r->c_str();
      if (auto* r = try_key(alt + "es")) return r->c_str();
    }
  }

  return "";
}

void Realmz_InsertMenuItem(MenuHandle theMenu, ConstStr255Param itemString, int16_t afterItem) {
  auto menu = mm.get_menu(theMenu);
  Menu::Item item;
  item.name = string_for_pstr<256>(itemString);
  item.enabled = true;
  menu->items.insert(menu->items.begin() + afterItem, item);
}

void Realmz_InsertSubmenuItem(MenuHandle theMenu, ConstStr255Param title, int16_t subMenuID, int16_t afterItem) {
  auto menu = mm.get_menu(theMenu);
  Menu::Item item;
  item.name = string_for_pstr<256>(title);
  item.key_equivalent = '\x1B';
  item.mark_character = static_cast<char>(subMenuID);
  item.enabled = true;
  menu->items.insert(menu->items.begin() + afterItem, item);
}

int32_t MenuManager_FindItemByKeyEquivalent(char ch) {
  return mm.find_item_by_key_equivalent(ch);
}

int PopulateScenarioMenu(MenuHandle theMenu) {
  auto menu = mm.get_menu(theMenu);

  // Strip any pre-existing scenario items (positions 5+; indices 4+).
  while (menu->items.size() > 4) {
    menu->items.pop_back();
  }

  // Traditional Fantasoft scenario ordering — unlisted entries sort alphabetically after these.
  static const std::vector<std::string> kOrder = {
      "City of Bywater",
      "Prelude to Pestilence",
      "Assault on Giant Mountain",
      "Destroy the Necronomicon",
      "Castle in the Clouds",
      "Grilochs Revenge",
      "White Dragon",
      "Mithril Vault",
      "Twin Sands of Time",
      "Trouble in the Sword Lands",
      "War in the Sword Lands",
      "Wrath of the Mind Lords",
      "Half Truth",
  };

  // Scan only the bundled :Scenarios: directory so that user-installed scenarios
  // from the userdata path (e.g. Kalypso's Island) are never shown.
  std::string scenarios_host = host_filename_for_mac_filename(":Scenarios:", false);

  std::vector<std::string> valid;
  if (std::filesystem::is_directory(scenarios_host)) {
    for (const auto& entry : std::filesystem::directory_iterator{scenarios_host}) {
      if (!entry.is_directory()) {
        continue;
      }
      std::string name = entry.path().filename().string();
      // A valid scenario directory contains a file with the same name as the folder.
      if (!std::filesystem::exists(entry.path() / name)) {
        continue;
      }
      valid.push_back(name);
    }
  }

  // Sort: known scenarios in traditional order first, then the rest alphabetically.
  auto order_idx = [&](const std::string& s) -> int {
    for (int i = 0; i < (int)kOrder.size(); i++) {
      if (s == kOrder[i]) return i;
    }
    return (int)kOrder.size();
  };
  std::sort(valid.begin(), valid.end(), [&](const std::string& a, const std::string& b) {
    int ia = order_idx(a), ib = order_idx(b);
    if (ia != ib) return ia < ib;
    std::string la = a, lb = b;
    std::transform(la.begin(), la.end(), la.begin(), ::tolower);
    std::transform(lb.begin(), lb.end(), lb.begin(), ::tolower);
    return la < lb;
  });

  for (const auto& name : valid) {
    auto& item = menu->items.emplace_back();
    item.name = name;
    item.enabled = false;
  }

  mm.sync();
  return (int)valid.size();
}
