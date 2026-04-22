#include "errhandlingapi.h"
#include "windef.h"
#include "winuser.h"
#include <memory>
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

  uint16_t i;
  for (auto menu : menu_list->menus) {
    auto submenu = CreateMenu();

    i = 1;
    for (const auto& submenu_item : menu->items) {
      UINT enabled_state = submenu_item.enabled ? MFS_ENABLED : MFS_DISABLED;
      UINT checked_state = submenu_item.checked ? MFS_CHECKED : MFS_UNCHECKED;

      std::string name = submenu_item.name;
      if (submenu_item.key_equivalent) {
        name += std::format("\tCtrl+{:c}", toupper(submenu_item.key_equivalent));
      }
      MENUITEMINFO submenu_info = MENUITEMINFO{
          .cbSize = sizeof(MENUITEMINFO),
          .fMask = MIIM_FTYPE | MIIM_ID | MIIM_STATE | MIIM_STRING,
          .fType = MFT_STRING,
          .fState = enabled_state | checked_state,
          .wID = PackMenuIdentifier(menu->menu_id, i),
          .dwTypeData = const_cast<char*>(name.c_str()),
          .cch = static_cast<UINT>(name.length())};

      InsertMenuItem(submenu, i++, TRUE, &submenu_info);
    }

    MENUITEMINFO item_info = MENUITEMINFO{
        .cbSize = sizeof(MENUITEMINFO),
        .fMask = MIIM_FTYPE | MIIM_ID | MIIM_STATE | MIIM_STRING | MIIM_SUBMENU,
        .fType = MFT_STRING,
        .fState = MFS_ENABLED,
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

  int i{0};
  for (const auto& item : menu->items) {
    i++;
    auto name = item.name.c_str();
    AppendMenu(popupMenu, (item.enabled ? MF_ENABLED : 0) | MF_STRING, i, name);
  }

  // TrackPopupMenu displays the menu in screen coordinates, not window coordinates. Rather
  // thank require the caller to convert the mouse position from local to global coordinates,
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
