#include "MenuManager.hpp"
#include "EventManager.h"
#include "FileManager.h"
#include "FileManager.hpp"
#include "MemoryManager.hpp"
#include "MenuController.h"
#include "MenuManager-C-Interface.h"
#include "ResourceManager.h"
#include "SDLMenuBar.hpp"
#include "StuffItArchive.hpp"
#include "StringConvert.hpp"
#include "WindowManager.hpp"
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <list>
#include <phosg/Strings.hh>
#include <resource_file/IndexFormats/Formats.hh>
#include <resource_file/ResourceFile.hh>
#include <stdexcept>
#include <unordered_map>

using ResourceDASM::ResourceFile;

static phosg::PrefixedLogger mm_log("[MenuManager] ");

// Name of the sub-folder under :Scenarios: that holds user-supplied scenarios.
static const char* const kThirdPartyFolder = "3rd Party Scenarios";

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

  Handle new_mbar_from_menu_ids(const int16_t* menu_ids, int count) {
    Handle handle = NewHandle(0);
    auto menu_list = std::make_shared<MenuList>();
    for (int i = 0; i < count; i++) {
      menu_list->menus.emplace_back(this->get_menu(menu_ids[i]));
    }
    this->handle_to_menulist.emplace(handle, menu_list);
    return handle;
  }

  void set_menu_item_key(Handle menu_handle, int16_t item, char key) {
    auto menu = this->get_menu(menu_handle);
    if (item >= 1 && item <= static_cast<int16_t>(menu->items.size())) {
      menu->items.at(item - 1).key_equivalent = key;
    }
  }

  void append_submenu_item_cstr(Handle menu_handle, const char* title, int16_t sub_menu_id) {
    auto menu = this->get_menu(menu_handle);
    Menu::Item item;
    item.name = title;
    item.key_equivalent = '\x1B';
    item.mark_character = static_cast<char>(sub_menu_id);
    item.enabled = true;
    menu->items.emplace_back(std::move(item));
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

// Decode a scenario's Mac custom folder icon (the "Icon\r" file's resource fork,
// extracted as "Icon.rsrc") into a 32x32-ish RGBA image suitable for the menu.
// Returns nullptr if no usable icon resource is present.
static std::shared_ptr<phosg::ImageRGBA8888N> load_scenario_folder_icon(const std::string& dir_mac) {
  std::string icon_host = host_filename_for_mac_filename(dir_mac + ":Icon.rsrc", false);
  if (!std::filesystem::is_regular_file(icon_host)) {
    return nullptr;
  }
  try {
    std::string data = phosg::load_file(icon_host);
    ResourceFile rf = ResourceDASM::parse_resource_fork(data);
    auto ids = rf.all_resources_of_type(ResourceDASM::RESOURCE_TYPE_icns);
    if (ids.empty()) {
      return nullptr;
    }
    auto decoded = rf.decode_icns(ids.front());

    // Prefer a 32x32 image (the classic icl8/ICN#), otherwise the largest one no
    // bigger than 32px, otherwise the smallest available — the menu scales to 32.
    auto& images = decoded.type_to_composite_image;
    auto best = images.end();
    auto score = [](size_t w) -> int { return w == 32 ? 0 : (w < 32 ? 1 : 2); };
    for (auto it = images.begin(); it != images.end(); ++it) {
      size_t iw = it->second.get_width();
      if (iw == 0 || it->second.get_height() == 0) continue;
      if (best == images.end()) {
        best = it;
        continue;
      }
      size_t bw = best->second.get_width();
      int sb = score(bw), si = score(iw);
      if (si < sb || (si == sb && ((si == 1 && iw > bw) || (si != 1 && iw < bw)))) {
        best = it;
      }
    }
    if (best == images.end()) {
      return nullptr;
    }
    return std::make_shared<phosg::ImageRGBA8888N>(std::move(best->second));
  } catch (const std::exception& e) {
    mm_log.warning_f("Failed to decode folder icon '{}': {}", icon_host, e.what());
    return nullptr;
  }
}

// Box-average downscale an image to a square of `target` pixels (the menu draws
// icons at a fixed 32px). Producing the final size here keeps the icon crisp and
// avoids caching a multi-hundred-pixel texture per scenario.
static std::shared_ptr<phosg::ImageRGBA8888N> downscale_square(const phosg::ImageRGBA8888N& src, size_t target) {
  size_t sw = src.get_width(), sh = src.get_height();
  if (sw == 0 || sh == 0) return nullptr;
  auto dst = std::make_shared<phosg::ImageRGBA8888N>(target, target);
  for (size_t y = 0; y < target; y++) {
    size_t sy0 = y * sh / target, sy1 = std::max(sy0 + 1, (y + 1) * sh / target);
    for (size_t x = 0; x < target; x++) {
      size_t sx0 = x * sw / target, sx1 = std::max(sx0 + 1, (x + 1) * sw / target);
      uint64_t r = 0, g = 0, b = 0, a = 0, n = 0;
      for (size_t yy = sy0; yy < sy1; yy++) {
        for (size_t xx = sx0; xx < sx1; xx++) {
          uint32_t c = src.read(xx, yy);
          r += (c >> 24) & 0xff;
          g += (c >> 16) & 0xff;
          b += (c >> 8) & 0xff;
          a += c & 0xff;
          n++;
        }
      }
      if (!n) n = 1;
      dst->write(x, y, ((r / n) << 24) | ((g / n) << 16) | ((b / n) << 8) | (a / n));
    }
  }
  return dst;
}

// Fall back to the scenario's splash picture (the 320x320-ish title image the
// engine shows on load, PICT 32128/30128/32127) when there's no custom folder
// icon. Downscaled to a 32px menu icon.
static std::shared_ptr<phosg::ImageRGBA8888N> load_scenario_splash_icon(const std::string& dir_mac) {
  std::string host = host_filename_for_mac_filename(dir_mac + ":Scenario.rsrc", false);
  if (!std::filesystem::is_regular_file(host)) {
    return nullptr;
  }
  try {
    std::string data = phosg::load_file(host);
    ResourceFile rf = ResourceDASM::parse_resource_fork(data);
    for (int16_t id : {(int16_t)32128, (int16_t)30128, (int16_t)32127}) {
      if (!rf.resource_exists(ResourceDASM::RESOURCE_TYPE_PICT, id)) continue;
      try {
        auto decoded = rf.decode_PICT(id);
        return downscale_square(decoded.image, 32);
      } catch (const std::exception&) {
        // Try the next candidate id.
      }
    }
  } catch (const std::exception& e) {
    mm_log.warning_f("Failed to read scenario splash '{}': {}", host, e.what());
  }
  return nullptr;
}

void SetItemIconFromScenarioPng(MenuHandle theMenu, int16_t item, const char* scenario_name) {
  auto menu = mm.get_menu(theMenu);
  if (item < 1 || item > static_cast<int16_t>(menu->items.size())) {
    return;
  }
  auto& menu_item = menu->items.at(item - 1);

  // Find a .png file in the scenario's directory. Bundled scenarios live directly
  // under :Scenarios:; 3rd party scenarios live under the "3rd Party Scenarios"
  // sub-folder, so fall back to there if nothing is found at the top level.
  std::string dir_mac = std::string(":Scenarios:") + scenario_name;
  auto files = mac_list_directory(dir_mac);
  if (files.empty()) {
    dir_mac = std::string(":Scenarios:") + kThirdPartyFolder + ":" + scenario_name;
    files = mac_list_directory(dir_mac);
  }
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
    // No bundled PNG. 3rd party scenarios (unpacked from .sit archives) carry
    // their icon either as a Mac custom folder icon, or — failing that — we use
    // the scenario's own splash picture as a representative icon.
    auto icon = load_scenario_folder_icon(dir_mac);
    if (!icon) icon = load_scenario_splash_icon(dir_mac);
    if (!icon) {
      mm_log.info_f("No icon found for scenario '{}'", scenario_name);
    }
    menu_item.icon_image = std::move(icon);
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

  result = -1;
  MCCreatePopupMenu(nullptr, m, mm.get_current_menu_list(), {top, left}, &popupCallback);

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

void SetMenuItemIsHeader(MenuHandle theMenu, int16_t item) {
  auto menu = mm.get_menu(theMenu);
  if (item < 1 || item > static_cast<int16_t>(menu->items.size())) return;
  auto& mi = menu->items.at(item - 1);
  mi.is_header = true;
  mi.enabled   = false;
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

void SetMenuItemShortcutText(MenuHandle theMenu, int16_t item, const char* shortcut_text) {
  auto menu = mm.get_menu(theMenu);
  if (item < 1 || item > static_cast<int16_t>(menu->items.size())) {
    return;
  }
  menu->items.at(item - 1).shortcut_text = shortcut_text ? shortcut_text : "";
}

// === Description file loader ===

static std::unordered_map<std::string, std::string> g_desc_cache;
static bool g_desc_cache_loaded = false;

static std::unordered_map<std::string, std::string> g_scenario_desc_cache;
static bool g_scenario_desc_cache_loaded = false;

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

static void load_scenario_descriptions_file() {
  FILE* f = mac_fopen(":Data Files:Scenario Descriptions.txt", "r");
  if (!f) {
    mm_log.warning_f("Could not open Scenario Descriptions.txt");
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
        g_scenario_desc_cache.emplace(current_key, std::move(current_body));
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

static const std::string* get_scenario_description(const std::string& name) {
  if (!g_scenario_desc_cache_loaded) {
    load_scenario_descriptions_file();
    g_scenario_desc_cache_loaded = true;
  }
  std::string key = desc_normalize_key(name);
  auto it = g_scenario_desc_cache.find(key);
  return (it != g_scenario_desc_cache.end()) ? &it->second : nullptr;
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

Handle Realmz_NewMBarFromMenus(const int16_t* menu_ids, int count) {
  return mm.new_mbar_from_menu_ids(menu_ids, count);
}

void SetMenuItemKey(MenuHandle theMenu, int16_t item, char key) {
  mm.set_menu_item_key(theMenu, item, key);
}

void AppendSubmenuItemCStr(MenuHandle theMenu, const char* title, int16_t subMenuID) {
  mm.append_submenu_item_cstr(theMenu, title, subMenuID);
}

// Unpack any *.sit scenario archives in the 3rd party folder that have not been
// extracted yet. third_party_host is the host path of the folder. A scenario is
// considered already installed if its folder contains a Scenario.rsrc fork.
static void install_third_party_scenarios(const std::string& third_party_host) {
  if (!std::filesystem::is_directory(third_party_host)) {
    return;
  }

  // Collect the archive list first: extraction creates sub-directories in this
  // same folder, and modifying a directory while iterating it is unspecified.
  std::vector<std::string> sit_paths;
  for (const auto& entry : std::filesystem::directory_iterator{third_party_host}) {
    if (!entry.is_regular_file()) {
      continue;
    }
    std::string ext = entry.path().extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".sit") {
      sit_paths.push_back(entry.path().string());
    }
  }

  for (const auto& sit_path : sit_paths) {
    std::string root = stuffit::root_folder_name(sit_path);
    if (root.empty()) {
      mm_log.warning_f("Could not read StuffIt archive '{}'", sit_path);
      continue;
    }

    std::filesystem::path scenario_dir = std::filesystem::path(third_party_host) / root;
    if (std::filesystem::is_regular_file(scenario_dir / "Scenario.rsrc")) {
      continue; // already extracted
    }

    mm_log.info_f("Extracting 3rd party scenario archive '{}'", sit_path);
    if (!stuffit::extract(sit_path, third_party_host)) {
      mm_log.warning_f("Failed to extract '{}'", sit_path);
    }
  }
}

// Returns the host path of the scenario's info PDF (in the :Manuals: folder) if
// one exists, else an empty string.
static std::string scenario_pdf_host_path(const std::string& name) {
  std::string host = host_filename_for_mac_filename(":Manuals:" + name + ".pdf", false);
  if (std::filesystem::is_regular_file(host)) {
    return host;
  }
  return "";
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

  // A scenario folder is playable only if it contains both a file matching the
  // folder name (the scenario data file) and a "Scenario.rsrc" resource fork.
  // The latter holds the RLMZ resources, pictures, etc.; without it the scenario
  // would crash on launch (e.g. scenarios copied off an old Mac that lost their
  // resource forks).
  auto is_playable_scenario = [](const std::filesystem::path& dir, const std::string& name) -> bool {
    return std::filesystem::exists(dir / name) &&
        std::filesystem::is_regular_file(dir / "Scenario.rsrc");
  };

  // Scan only the bundled :Scenarios: directory so that user-installed scenarios
  // from the userdata path are never shown.
  std::string scenarios_host = host_filename_for_mac_filename(":Scenarios:", false);

  std::vector<std::string> valid;
  if (std::filesystem::is_directory(scenarios_host)) {
    for (const auto& entry : std::filesystem::directory_iterator{scenarios_host}) {
      if (!entry.is_directory()) {
        continue;
      }
      std::string name = entry.path().filename().string();
      // The "3rd Party Scenarios" folder is a container, not a scenario itself;
      // it is scanned separately below.
      if (name == kThirdPartyFolder) {
        continue;
      }
      if (!is_playable_scenario(entry.path(), name)) {
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
    if (const std::string* desc = get_scenario_description(name))
      item.description = *desc;
    item.pdf_path = scenario_pdf_host_path(name);
  }

  // The Fantasoft scenario count determines topfantasoftsceanrio; anything past
  // the divider added below is treated as a 3rd-party scenario by the engine.
  int fantasoft_count = (int)valid.size();

  // Scan the :Scenarios:3rd Party Scenarios: folder for additional, user-supplied
  // scenarios. These are listed below a divider in plain alphabetical order.
  std::string third_party_host =
      host_filename_for_mac_filename(std::string(":Scenarios:") + kThirdPartyFolder + ":", false);

  // 3rd party scenarios are distributed as StuffIt (.sit) archives so that their
  // Mac resource forks survive being stored on non-Mac filesystems. Extract any
  // that haven't been unpacked yet into sibling folders, which the scan below
  // then discovers just like a normal scenario directory.
  install_third_party_scenarios(third_party_host);

  std::vector<std::string> third_party;
  if (std::filesystem::is_directory(third_party_host)) {
    for (const auto& entry : std::filesystem::directory_iterator{third_party_host}) {
      if (!entry.is_directory()) {
        continue;
      }
      std::string name = entry.path().filename().string();
      if (!is_playable_scenario(entry.path(), name)) {
        mm_log.warning_f("Skipping 3rd party scenario '{}' (missing data or Scenario.rsrc)", name);
        continue;
      }
      third_party.push_back(name);
    }
  }

  std::sort(third_party.begin(), third_party.end(), [](const std::string& a, const std::string& b) {
    std::string la = a, lb = b;
    std::transform(la.begin(), la.end(), la.begin(), ::tolower);
    std::transform(lb.begin(), lb.end(), lb.begin(), ::tolower);
    return la < lb;
  });

  if (!third_party.empty()) {
    // Separate the bundled Fantasoft scenarios from the 3rd party scenarios with an
    // unselectable "3rd Party Scenarios" heading sandwiched between two dividers.
    // The heading is a disabled, non-"-" item; updatescenarioavail() leaves it
    // disabled because selectscenario() finds no scenario data file of that name.
    auto& sep_top = menu->items.emplace_back();
    sep_top.name = "-";
    sep_top.enabled = false;

    auto& header = menu->items.emplace_back();
    header.name = "3rd Party Scenarios";
    header.enabled = false;

    auto& sep_bottom = menu->items.emplace_back();
    sep_bottom.name = "-";
    sep_bottom.enabled = false;

    for (const auto& name : third_party) {
      auto& item = menu->items.emplace_back();
      item.name = name;
      item.enabled = false;
      if (const std::string* desc = get_scenario_description(name))
        item.description = *desc;
      item.pdf_path = scenario_pdf_host_path(name);
    }
  }

  mm.sync();
  return fantasoft_count;
}
