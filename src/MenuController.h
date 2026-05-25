#pragma once

#include "../MenuManager.hpp"
#include <SDL3/SDL_video.h>
#include <memory>

#ifdef __cplusplus
extern "C" {
#endif

void MCSync(std::shared_ptr<MenuList> menuList, void (*callback)(int16_t, int16_t));
void MCCreatePopupMenu(void* nsWindow, std::shared_ptr<Menu> menu, std::shared_ptr<MenuList> submenus, std::pair<int16_t, int16_t> loc, void (*callback)(int16_t, int16_t));
// Install the platform window hook needed for owner-drawn popup menus.
// Must be called once after the SDL window is created, before any PopUpMenuSelect calls.
void MCInstallWindowHook(SDL_Window* sdl_window, void (*callback)(int16_t, int16_t));

#ifdef __cplusplus
}
#endif