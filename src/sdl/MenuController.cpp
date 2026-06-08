#include "MenuController.h"
#include "SDLMenuBar.hpp"

// MCSync is a no-op on SDL platforms: SDLMenuBar handles all menu bar display.
void MCSync(std::shared_ptr<MenuList>, void (*)(int16_t, int16_t)) {}

// MCInstallWindowHook is a no-op on SDL platforms: SDLMenuBar handles keyboard
// shortcuts and menu events through the normal SDL event loop.
void MCInstallWindowHook(SDL_Window*, void (*)(int16_t, int16_t)) {}

// MCCreatePopupMenu shows a blocking SDL popup menu and calls callback with the
// result before returning, matching the synchronous Win32/Cocoa contract.
void MCCreatePopupMenu(
    void* /*nsWindow*/,
    std::shared_ptr<Menu> menu,
    std::shared_ptr<MenuList> /*submenus*/,
    std::pair<int16_t, int16_t> /*loc*/,
    void (*callback)(int16_t, int16_t)) {
  int16_t item_id = SDLMenuBar::instance().run_popup_select(menu);
  callback(menu ? menu->menu_id : 0, item_id);
}
