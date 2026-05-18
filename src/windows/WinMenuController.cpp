#include "errhandlingapi.h"
#include "windef.h"
#include "wingdi.h"
#include "winuser.h"
#include <memory>
#include <unordered_map>
#include <phosg/Image.hh>
#include <phosg/Strings.hh>

#include "./WinMenuController.hpp"
#include <utility>

static phosg::PrefixedLogger wmc_log("[WinMenuController] ");

// Static variable to keep the original window proc
static WNDPROC g_OldWndProc = nullptr;

// Callback to invoke with clicked menu items. Should be a pointer to a function that
// accepts two int16_t params, the menu_id and the item_id (which is the 1-indexed position of
// the item in the menu)
static void (*menuCallback)(int16_t, int16_t){};

// Current menu list for keyboard shortcut lookup
static std::shared_ptr<WinMenuList> current_menu_list;

// Packs the menu id and item id of each submenu item into a single word. When a command menu
// item is clicked, Windows sends a WM_COMMAND message with the low byte of the wParam filled
// with the wID property of the MENUITEMINFO struct of the menu. By packing both the Realmz
// menu_id and the position of the submenu as the item_id, we can extract these values when
// handling WM_COMMAND messages and convert them into synthetic menu click events to send
// to the Realmz event loop.
WORD PackMenuIdentifier(int8_t menu_id, int8_t item_id) {
  return (menu_id << 8) | item_id;
}

// Returns a pair with the menu_id and item_id from a packed wParam
std::pair<int16_t, int16_t> UnpackMenuIdentifier(WORD wParam) {
  return {(wParam >> 8) & 0x00FF, wParam & 0x00FF};
}

// Returns {menu_id, item_id}, or {0, 0} if not found
std::pair<int16_t, int16_t> FindMenuItemByKeyEquivalent(char ch) {
  wmc_log.info_f("Looking for menu item with key {:c} ({:02X})", ch, ch);
  if (!current_menu_list) {
    wmc_log.info_f("No menus are loaded");
    return {0, 0};
  }

  ch = toupper(ch);
  for (const auto& menu_set : {current_menu_list->menus, current_menu_list->submenus}) {
    for (const auto& menu : menu_set) {
      wmc_log.info_f("Looking in menu \"{}\"", menu->title);
      if (!menu->enabled) {
        continue;
      }
      for (size_t z = 0; z < menu->items.size(); z++) {
        const auto& item = menu->items[z];
        wmc_log.info_f("Looking at item \"{}\" -> \"{}\" ({:02X})", menu->title, item.name, item.key_equivalent, ch);
        if (item.enabled && toupper(item.key_equivalent) == ch) {
          wmc_log.info_f("Found menu item ID ({}, {})", menu->menu_id, z);
          return {menu->menu_id, z + 1};
        }
      }
    }
  }

  wmc_log.info_f("No menu item matched the given key");
  return {0, 0};
}

LRESULT CALLBACK RealmzWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_COMMAND) {
    if (menuCallback != nullptr) {
      auto identifier_pair = UnpackMenuIdentifier(wParam);
      menuCallback(identifier_pair.first, identifier_pair.second);
    }
    return 0;
  }

  if ((msg == WM_KEYDOWN) || (msg == WM_SYSKEYDOWN)) {
    wmc_log.info_f("WM_(SYS)?KEYDOWN: wParam = {:04X}, menuCallback present = {}", wParam, (menuCallback != nullptr));
  }
  if (((msg == WM_KEYDOWN) || (msg == WM_SYSKEYDOWN)) && (menuCallback != nullptr) && (GetKeyState(VK_CONTROL) & 0x8000)) {
    char ch = static_cast<char>(wParam);

    auto menu_item = FindMenuItemByKeyEquivalent(ch);
    if (menu_item.first != 0) {
      wmc_log.info_f("Received menu keyboard shortcut: Ctrl+{} -> menu={}, item={}",
          ch, menu_item.first, menu_item.second);
      menuCallback(menu_item.first, menu_item.second);
      return 0;
    }
  }

  // Forward everything else to the original WndProc
  return CallWindowProc(g_OldWndProc, hwnd, msg, wParam, lParam);
}

void HookWndProc(HWND hwnd) {
  if (g_OldWndProc == nullptr) {
    SetLastError(0);
    g_OldWndProc = (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)RealmzWndProc);
    if (g_OldWndProc == nullptr) {
      wmc_log.error_f("Could not hook custom proc: %s", GetLastError());
    }
  }
}

HWND get_window_handle(SDL_Window* sdl_window) {
  auto props = SDL_GetWindowProperties(sdl_window);
  return reinterpret_cast<HWND>(SDL_GetPointerProperty(
      props,
      SDL_PROP_WINDOW_WIN32_HWND_POINTER,
      NULL));
}

static std::unordered_map<uint8_t, HBITMAP> icon_hbm_cache;

static HBITMAP make_icon_hbitmap(const phosg::ImageRGBA8888N& img) {
  int w = static_cast<int>(img.get_width());
  int h = static_cast<int>(img.get_height());

  BITMAPINFOHEADER bmi = {};
  bmi.biSize        = sizeof(bmi);
  bmi.biWidth       = w;
  bmi.biHeight      = -h;
  bmi.biPlanes      = 1;
  bmi.biBitCount    = 24;
  bmi.biCompression = BI_RGB;

  void* bits = nullptr;
  HBITMAP hbm = CreateDIBSection(nullptr, reinterpret_cast<BITMAPINFO*>(&bmi),
      DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!hbm || !bits) {
    return nullptr;
  }

  // Composite the icon against the system menu background color so that
  // transparent pixels blend cleanly (hbmpItem uses BitBlt, not AlphaBlend).
  COLORREF bg = GetSysColor(COLOR_MENU);
  uint8_t bg_r = GetRValue(bg);
  uint8_t bg_g = GetGValue(bg);
  uint8_t bg_b = GetBValue(bg);

  // phosg RGBA8888N: 0xRRGGBBAA (R in the most-significant byte).
  // 24-bit DIB rows are DWORD-aligned, stored as BGR bytes.
  const uint32_t* src = static_cast<const uint32_t*>(static_cast<const void*>(img.get_data()));
  int row_bytes = (w * 3 + 3) & ~3;
  uint8_t* dst = static_cast<uint8_t*>(bits);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      uint32_t p = src[y * w + x];
      uint8_t r = (p >> 24) & 0xFF;
      uint8_t g = (p >> 16) & 0xFF;
      uint8_t b = (p >>  8) & 0xFF;
      uint8_t a = (p      ) & 0xFF;
      uint8_t* out = dst + y * row_bytes + x * 3;
      out[0] = static_cast<uint8_t>((b * a + bg_b * (255 - a)) / 255);
      out[1] = static_cast<uint8_t>((g * a + bg_g * (255 - a)) / 255);
      out[2] = static_cast<uint8_t>((r * a + bg_r * (255 - a)) / 255);
    }
  }
  return hbm;
}

static HMENU build_win_menu_items(const std::shared_ptr<WinMenuList>& menu_list, const std::shared_ptr<WinMenu>& menu) {
  HMENU hmenu = CreatePopupMenu();
  uint16_t i = 1;
  for (const auto& item : menu->items) {
    bool is_separator = (item.name == "-" || item.name == "(-" ||
        (item.name.size() >= 2 && item.name[0] == '(' && item.name[1] == '-'));

    if (is_separator) {
      MENUITEMINFO sep_info = MENUITEMINFO{
          .cbSize = sizeof(MENUITEMINFO),
          .fMask = MIIM_FTYPE | MIIM_ID,
          .fType = MFT_SEPARATOR,
          .wID = PackMenuIdentifier(menu->menu_id, i)};
      InsertMenuItem(hmenu, i++, TRUE, &sep_info);
      continue;
    }

    if (item.key_equivalent == '\x1B') {
      int16_t sub_id = static_cast<uint8_t>(item.mark_character);
      HMENU sub_hmenu = NULL;
      for (const auto& sub : menu_list->submenus) {
        if (sub->menu_id == sub_id) {
          sub_hmenu = build_win_menu_items(menu_list, sub);
          break;
        }
      }
      std::string name = item.name;
      MENUITEMINFO item_info = MENUITEMINFO{
          .cbSize = sizeof(MENUITEMINFO),
          .fMask = MIIM_FTYPE | MIIM_ID | MIIM_STATE | MIIM_STRING | MIIM_SUBMENU,
          .fType = MFT_STRING,
          .fState = static_cast<UINT>(item.enabled ? MFS_ENABLED : MFS_DISABLED),
          .wID = PackMenuIdentifier(menu->menu_id, i),
          .hSubMenu = sub_hmenu,
          .dwTypeData = const_cast<char*>(name.c_str()),
          .cch = static_cast<UINT>(name.length())};
      InsertMenuItem(hmenu, i++, TRUE, &item_info);
      continue;
    }

    UINT enabled_state = item.enabled ? MFS_ENABLED : MFS_DISABLED;
    UINT checked_state = item.checked ? MFS_CHECKED : MFS_UNCHECKED;
    std::string name = item.name;
    if (item.key_equivalent) {
      name += std::format("\tCtrl+{:c}", toupper(item.key_equivalent));
    }
    MENUITEMINFO item_info = MENUITEMINFO{
        .cbSize = sizeof(MENUITEMINFO),
        .fMask = MIIM_FTYPE | MIIM_ID | MIIM_STATE | MIIM_STRING,
        .fType = MFT_STRING,
        .fState = enabled_state | checked_state,
        .wID = PackMenuIdentifier(menu->menu_id, i),
        .dwTypeData = const_cast<char*>(name.c_str()),
        .cch = static_cast<UINT>(name.length())};
    if (item.icon_image) {
      HBITMAP hbm = nullptr;
      auto it = icon_hbm_cache.find(item.icon_number);
      if (it != icon_hbm_cache.end()) {
        hbm = it->second;
      } else {
        hbm = make_icon_hbitmap(*item.icon_image);
        icon_hbm_cache[item.icon_number] = hbm;
      }
      if (hbm) {
        item_info.fMask |= MIIM_BITMAP;
        item_info.hbmpItem = hbm;
      }
    }
    InsertMenuItem(hmenu, i++, TRUE, &item_info);
  }
  return hmenu;
}

void WinMenuSync(SDL_Window* sdl_window, std::shared_ptr<WinMenuList> menu_list, void (*callback)(int16_t, int16_t)) {
  // Update current menu click callback function
  menuCallback = callback;

  // Store the current menu list for keyboard shortcut lookup
  current_menu_list = menu_list;

  auto wind_handle = get_window_handle(sdl_window);

  HMENU win_menu = CreateMenu();
  MENUINFO win_menu_info = MENUINFO{
      .cbSize = sizeof(MENUINFO),
      .fMask = MIM_APPLYTOSUBMENUS | MIM_STYLE};
  SetMenuInfo(win_menu, &win_menu_info);

  for (auto menu : menu_list->menus) {
    auto submenu = build_win_menu_items(menu_list, menu);

    MENUITEMINFO item_info = MENUITEMINFO{
        .cbSize = sizeof(MENUITEMINFO),
        .fMask = MIIM_FTYPE | MIIM_ID | MIIM_STATE | MIIM_STRING | MIIM_SUBMENU,
        .fType = MFT_STRING,
        .fState = static_cast<UINT>(menu->enabled ? MFS_ENABLED : MFS_DISABLED),
        .wID = static_cast<UINT>(menu->menu_id),
        .hSubMenu = submenu,
        .hbmpChecked = NULL,
        .hbmpUnchecked = NULL,
        .dwItemData = NULL,
        .dwTypeData = const_cast<char*>(menu->title.c_str()),
        .cch = static_cast<UINT>(menu->title.length()),
        .hbmpItem = NULL};
    InsertMenuItem(win_menu, menu->menu_id, FALSE, &item_info);
  }

  auto old_menu = GetMenu(wind_handle);
  SetMenu(wind_handle, win_menu);
  HookWndProc(wind_handle);

  DrawMenuBar(wind_handle);

  if (old_menu) {
    DestroyMenu(old_menu);
  }

  // After experimenting with this, it seems that calls to SDL_SetWindowSize actually set the
  // client area of the window, not the full window size inclusive of the menu bar. Since we have to
  // bypass SDL to create the menu directly via the Windows API, it seems that SDL doesn't know that
  // the rendering of the menu bar has shrunk the client area. So, a quick call to SDL_SetWindowSize is
  // enough to force SDL to realize the menu bar now exists and to expand the window to ensure that the
  // client area is the full 800x600.
  SDL_SetWindowSize(sdl_window, 800, 600);
}

int WinCreatePopupMenu(SDL_Window* sdl_window, std::shared_ptr<WinMenu> menu) {
  auto wind_handle = get_window_handle(sdl_window);

  HMENU popupMenu = CreatePopupMenu();

  int i = 0;
  for (const auto& item : menu->items) {
    i++;
    UINT state = (item.enabled ? MFS_ENABLED : MFS_DISABLED)
               | (item.checked ? MFS_CHECKED : MFS_UNCHECKED)
               | ((item.style_flags & 1) ? MFS_DEFAULT : 0);
    std::string name = item.name;
    MENUITEMINFO item_info = MENUITEMINFO{
        .cbSize = sizeof(MENUITEMINFO),
        .fMask = MIIM_FTYPE | MIIM_ID | MIIM_STATE | MIIM_STRING,
        .fType = MFT_STRING,
        .fState = state,
        .wID = static_cast<UINT>(i),
        .dwTypeData = const_cast<char*>(name.c_str()),
        .cch = static_cast<UINT>(name.length())};
    if (item.icon_image) {
      HBITMAP hbm = nullptr;
      auto it = icon_hbm_cache.find(item.icon_number);
      if (it != icon_hbm_cache.end()) {
        hbm = it->second;
      } else {
        hbm = make_icon_hbitmap(*item.icon_image);
        icon_hbm_cache[item.icon_number] = hbm;
      }
      if (hbm) {
        item_info.fMask |= MIIM_BITMAP;
        item_info.hbmpItem = hbm;
      }
    }
    InsertMenuItem(popupMenu, i, TRUE, &item_info);
  }

  // TrackPopupMenu displays the menu in screen coordinates, not window coordinates. Rather
  // than require the caller to convert the mouse position from local to global coordinates,
  // it's easier to just get the mouse position fresh right here.
  POINT pt;
  GetCursorPos(&pt);

  int result = TrackPopupMenu(popupMenu,
      TPM_RETURNCMD | TPM_RIGHTBUTTON,
      pt.x, pt.y,
      0,
      wind_handle,
      NULL);

  DestroyMenu(popupMenu);

  return result;
}
