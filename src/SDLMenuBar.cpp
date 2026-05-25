#include "SDLMenuBar.hpp"

#include "EventManager.h"
#include "Font.hpp"
#include "MenuManager-C-Interface.h"
#include "WindowManager.hpp"

#include <SDL3/SDL.h>
#include <phosg/Strings.hh>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

static phosg::PrefixedLogger smb_log("[SDLMenuBar] ");

// ── Colors ────────────────────────────────────────────────────────────────────

static constexpr SDL_Color COLOR_BAR_BG    = {0x2D, 0x2D, 0x2D, 0xFF};
static constexpr SDL_Color COLOR_BAR_EDGE  = {0x1A, 0x1A, 0x1A, 0xFF};
static constexpr SDL_Color COLOR_TITLE_HL  = {0x4A, 0x4A, 0x8A, 0xFF};
static constexpr SDL_Color COLOR_DROP_BG   = {0x3D, 0x3D, 0x3D, 0xFF};
static constexpr SDL_Color COLOR_DROP_BDR  = {0x55, 0x55, 0x55, 0xFF};
static constexpr SDL_Color COLOR_SHADOW    = {0x18, 0x18, 0x18, 0x80};
static constexpr SDL_Color COLOR_ITEM_HL   = {0x44, 0x55, 0xCC, 0xFF};
static constexpr SDL_Color COLOR_SEP       = {0x55, 0x55, 0x55, 0xFF};
static constexpr SDL_Color COLOR_WHITE     = {0xFF, 0xFF, 0xFF, 0xFF};
static constexpr SDL_Color COLOR_GRAY      = {0x88, 0x88, 0x88, 0xFF};
static constexpr SDL_Color COLOR_SHORTCUT  = {0x99, 0x99, 0x99, 0xFF};

// ── Helpers ───────────────────────────────────────────────────────────────────

static void set_draw_color(SDL_Renderer* r, SDL_Color c) {
  SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

static void fill_frect(SDL_Renderer* r, float x, float y, float w, float h) {
  SDL_FRect fr{x, y, w, h};
  SDL_RenderFillRect(r, &fr);
}

static bool is_separator(const Menu::Item& item) {
  return item.name == "-" || item.name.empty();
}

static bool is_submenu(const Menu::Item& item) {
  return item.key_equivalent == '\x1B';
}

static std::string shortcut_label(const Menu::Item& item) {
  if (!item.key_equivalent || is_submenu(item)) return "";
  char buf[16];
  snprintf(buf, sizeof(buf), "Ctrl+%c", (char)toupper((unsigned char)item.key_equivalent));
  return buf;
}

// ── Singleton ─────────────────────────────────────────────────────────────────

SDLMenuBar& SDLMenuBar::instance() {
  static SDLMenuBar inst;
  return inst;
}

// ── Public interface ──────────────────────────────────────────────────────────

void SDLMenuBar::init(TTF_Font* f) {
  this->font = f;
}

void SDLMenuBar::sync(std::shared_ptr<MenuList> ml) {
  this->menu_list = ml;
  this->title_layout_win_w = 0; // force rebuild
  this->destroy_icon_cache();
  // Close any open dropdown so stale indices aren't used
  this->open_menu_idx = -1;
  this->hovered_item_idx = -1;
}

int SDLMenuBar::reserved_top_pixels(bool fullscreen) {
  return fullscreen ? 0 : MENUBAR_HEIGHT;
}

// ── Animation ─────────────────────────────────────────────────────────────────

void SDLMenuBar::on_fullscreen_changed(bool now_fullscreen) {
  this->y_offset = now_fullscreen ? (float)-MENUBAR_HEIGHT : 0.0f;
  this->y_target = this->y_offset;
  this->open_menu_idx = -1;
  this->hovered_item_idx = -1;
  this->auto_hide_timer = 0.0f;
}

void SDLMenuBar::update(float dt, int cursor_y, bool fullscreen) {
  if (!fullscreen) {
    this->y_offset = 0.0f;
    this->y_target = 0.0f;
    return;
  }

  bool cursor_in_trigger = (cursor_y < TRIGGER_ZONE_PX);
  bool cursor_in_bar = (cursor_y >= 0 && (float)cursor_y <= MENUBAR_HEIGHT + this->y_offset);
  bool menu_open = (this->open_menu_idx >= 0);

  if (cursor_in_trigger || cursor_in_bar || menu_open) {
    this->y_target = 0.0f;
    this->auto_hide_timer = 0.0f;
  } else {
    this->auto_hide_timer += dt;
    if (this->auto_hide_timer >= AUTO_HIDE_DELAY_S) {
      this->y_target = (float)-MENUBAR_HEIGHT;
    }
  }

  if (this->y_offset != this->y_target) {
    float speed = (float)MENUBAR_HEIGHT / ANIM_DURATION_S;
    float step = speed * dt;
    if (this->y_target > this->y_offset) {
      this->y_offset = std::min(this->y_offset + step, this->y_target);
    } else {
      this->y_offset = std::max(this->y_offset - step, this->y_target);
    }
  }
}

// ── Layout ────────────────────────────────────────────────────────────────────

std::vector<std::shared_ptr<Menu>> SDLMenuBar::get_top_menus() const {
  if (!this->menu_list) return {};
  return {this->menu_list->menus.begin(), this->menu_list->menus.end()};
}

void SDLMenuBar::rebuild_title_layouts(int win_w) {
  if (!this->font || win_w == this->title_layout_win_w) return;
  this->title_layout_win_w = win_w;
  this->title_layouts.clear();

  TTF_SetFontSize(this->font, 16);
  int x = 8;
  for (auto& menu : this->get_top_menus()) {
    int tw = this->measure_text_width(menu->title);
    int bw = tw + 2 * TITLE_HPAD;
    this->title_layouts.push_back({x, bw});
    x += bw;
  }
}

int SDLMenuBar::dropdown_width(int menu_idx) const {
  if (!this->font) return DROPDOWN_MIN_W;
  auto menus = this->get_top_menus();
  if (menu_idx < 0 || menu_idx >= (int)menus.size()) return DROPDOWN_MIN_W;
  const auto& menu = menus[menu_idx];
  int w = DROPDOWN_MIN_W;
  for (const auto& item : menu->items) {
    if (is_separator(item)) continue;
    int iw = ITEM_LPAD + this->measure_text_width(item.name) + ITEM_RPAD;
    std::string sc = shortcut_label(item);
    if (!sc.empty()) {
      iw += this->measure_text_width(sc);
    }
    w = std::max(w, iw);
  }
  return w;
}

int SDLMenuBar::dropdown_height(int menu_idx) const {
  auto menus = this->get_top_menus();
  if (menu_idx < 0 || menu_idx >= (int)menus.size()) return 0;
  const auto& menu = menus[menu_idx];
  int h = 4;
  for (const auto& item : menu->items) {
    h += is_separator(item) ? SEP_H : ITEM_H;
  }
  return h;
}

int SDLMenuBar::dropdown_x(int menu_idx, int win_w) const {
  if (menu_idx < 0 || menu_idx >= (int)this->title_layouts.size()) return 0;
  int x = this->title_layouts[menu_idx].x;
  int w = this->dropdown_width(menu_idx);
  if (x + w > win_w) x = win_w - w;
  if (x < 0) x = 0;
  return x;
}

// ── Hit testing ───────────────────────────────────────────────────────────────

int SDLMenuBar::hit_test_bar(float px, float py) const {
  float bt = this->y_offset;
  if (py < bt || py >= bt + MENUBAR_HEIGHT) return -1;
  for (int i = 0; i < (int)this->title_layouts.size(); i++) {
    const auto& tl = this->title_layouts[i];
    if (px >= tl.x && px < tl.x + tl.width) return i;
  }
  return -1;
}

int SDLMenuBar::hit_test_dropdown(float px, float py, int win_w, int win_h) const {
  if (this->open_menu_idx < 0) return -1;
  float bt = this->y_offset;
  float dx = (float)this->dropdown_x(this->open_menu_idx, win_w);
  float dw = (float)this->dropdown_width(this->open_menu_idx);
  float drop_y_start = bt + MENUBAR_HEIGHT;
  int dh = this->dropdown_height(this->open_menu_idx);
  if (py < drop_y_start || py >= drop_y_start + dh) return -1;
  if (px < dx || px >= dx + dw) return -1;

  auto menus = this->get_top_menus();
  if (this->open_menu_idx >= (int)menus.size()) return -1;
  const auto& menu = menus[this->open_menu_idx];
  float iy = drop_y_start + 2.0f;
  for (int i = 0; i < (int)menu->items.size(); i++) {
    float item_h = is_separator(menu->items[i]) ? (float)SEP_H : (float)ITEM_H;
    if (py >= iy && py < iy + item_h) {
      if (!is_separator(menu->items[i])) return i;
      return -1;
    }
    iy += item_h;
  }
  return -1;
}

// ── Drawing ───────────────────────────────────────────────────────────────────

void SDLMenuBar::draw_text(SDL_Renderer* r, const std::string& text, int x, int y, SDL_Color color) const {
  if (!this->font || text.empty()) return;
  SDL_Surface* surf = TTF_RenderText_Blended(this->font, text.c_str(), text.size(), color);
  if (!surf) return;
  SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
  if (tex) {
    SDL_FRect dst{(float)x, (float)y, (float)surf->w, (float)surf->h};
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_RenderTexture(r, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
  }
  SDL_DestroySurface(surf);
}

int SDLMenuBar::measure_text_width(const std::string& text) const {
  if (!this->font || text.empty()) return 0;
  int w = 0, h = 0;
  TTF_GetStringSize(this->font, text.c_str(), text.size(), &w, &h);
  return w;
}

SDL_Texture* SDLMenuBar::get_icon_texture(SDL_Renderer* r, const phosg::ImageRGBA8888N* img) {
  if (!img) return nullptr;
  if (this->cached_renderer != r) {
    this->destroy_icon_cache();
    this->cached_renderer = r;
  }
  auto it = this->icon_cache.find(img);
  if (it != this->icon_cache.end()) return it->second;

  int w = (int)img->get_width();
  int h = (int)img->get_height();
  SDL_Surface* surf = SDL_CreateSurfaceFrom(
      w, h, SDL_PIXELFORMAT_RGBA8888,
      const_cast<uint32_t*>(img->get_data()),
      4 * w);
  if (!surf) return nullptr;
  SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
  SDL_DestroySurface(surf);
  if (!tex) return nullptr;
  SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
  this->icon_cache[img] = tex;
  return tex;
}

void SDLMenuBar::destroy_icon_cache() {
  for (auto& [key, tex] : this->icon_cache) {
    if (tex) SDL_DestroyTexture(tex);
  }
  this->icon_cache.clear();
}

void SDLMenuBar::draw_bar_strip(SDL_Renderer* r, int win_w) {
  float bt = this->y_offset;
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

  // Background
  set_draw_color(r, COLOR_BAR_BG);
  fill_frect(r, 0, bt, (float)win_w, (float)MENUBAR_HEIGHT);

  // Bottom edge
  set_draw_color(r, COLOR_BAR_EDGE);
  SDL_RenderLine(r, 0, (int)(bt + MENUBAR_HEIGHT - 1), win_w - 1, (int)(bt + MENUBAR_HEIGHT - 1));

  if (!this->font) return;
  TTF_SetFontSize(this->font, 16);
  TTF_SetFontStyle(this->font, TTF_STYLE_NORMAL);

  auto menus = this->get_top_menus();
  for (int i = 0; i < (int)menus.size(); i++) {
    const auto& menu = menus[i];
    if (i >= (int)this->title_layouts.size()) break;
    const auto& tl = this->title_layouts[i];
    bool highlighted = (i == this->open_menu_idx);

    if (highlighted) {
      SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
      set_draw_color(r, COLOR_TITLE_HL);
      fill_frect(r, (float)tl.x, bt + 1.0f, (float)tl.width, (float)(MENUBAR_HEIGHT - 2));
    }

    int tw = this->measure_text_width(menu->title);
    int tx = tl.x + (tl.width - tw) / 2;
    int ty = (int)(bt + (MENUBAR_HEIGHT - 16) / 2.0f);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Color col = menu->enabled ? COLOR_WHITE : COLOR_GRAY;
    this->draw_text(r, menu->title, tx, ty, col);
  }
}

void SDLMenuBar::draw_dropdown(SDL_Renderer* r, int win_w, int win_h) {
  if (this->open_menu_idx < 0 || !this->font) return;
  auto menus = this->get_top_menus();
  if (this->open_menu_idx >= (int)menus.size()) return;
  const auto& menu = menus[this->open_menu_idx];

  float bt = this->y_offset;
  int dx = this->dropdown_x(this->open_menu_idx, win_w);
  int dw = this->dropdown_width(this->open_menu_idx);
  int dh = this->dropdown_height(this->open_menu_idx);
  float drop_y = bt + MENUBAR_HEIGHT;

  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

  // Drop shadow
  set_draw_color(r, COLOR_SHADOW);
  fill_frect(r, (float)(dx + 3), drop_y + 3.0f, (float)dw, (float)dh);

  // Panel background
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
  set_draw_color(r, COLOR_DROP_BG);
  fill_frect(r, (float)dx, drop_y, (float)dw, (float)dh);

  // Panel border
  set_draw_color(r, COLOR_DROP_BDR);
  SDL_FRect border{(float)dx, drop_y, (float)dw, (float)dh};
  SDL_RenderRect(r, &border);

  TTF_SetFontSize(this->font, 16);
  TTF_SetFontStyle(this->font, TTF_STYLE_NORMAL);

  float iy = drop_y + 2.0f;
  for (int i = 0; i < (int)menu->items.size(); i++) {
    const auto& item = menu->items[i];
    if (is_separator(item)) {
      SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
      set_draw_color(r, COLOR_SEP);
      SDL_RenderLine(r, dx + 4, (int)(iy + SEP_H / 2), dx + dw - 4, (int)(iy + SEP_H / 2));
      iy += SEP_H;
      continue;
    }

    bool hovered = (i == this->hovered_item_idx && item.enabled);
    if (hovered) {
      SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
      set_draw_color(r, COLOR_ITEM_HL);
      fill_frect(r, (float)(dx + 1), iy, (float)(dw - 2), (float)ITEM_H);
    }

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Color text_col = item.enabled ? COLOR_WHITE : COLOR_GRAY;
    int text_y = (int)(iy + (ITEM_H - 16) / 2.0f);

    // Checkmark
    if (item.checked) {
      this->draw_text(r, "\xe2\x9c\x93", dx + 5, text_y, text_col); // UTF-8 ✓
    }

    // Icon (16×16)
    if (item.icon_image) {
      SDL_Texture* tex = this->get_icon_texture(r, item.icon_image.get());
      if (tex) {
        SDL_FRect icon_dst{(float)(dx + 13), iy + (ITEM_H - 16) / 2.0f, 16.0f, 16.0f};
        SDL_RenderTexture(r, tex, nullptr, &icon_dst);
      }
    }

    // Item name
    this->draw_text(r, item.name, dx + ITEM_LPAD, text_y, text_col);

    // Submenu arrow or keyboard shortcut
    if (is_submenu(item)) {
      this->draw_text(r, "\xe2\x96\xb6", dx + dw - 14, text_y, text_col); // UTF-8 ▶
    } else {
      std::string sc = shortcut_label(item);
      if (!sc.empty()) {
        int sw = this->measure_text_width(sc);
        this->draw_text(r, sc, dx + dw - ITEM_RPAD / 2 - sw / 2, text_y, COLOR_SHORTCUT);
      }
    }

    iy += ITEM_H;
  }
}

void SDLMenuBar::draw(SDL_Renderer* r, int win_w, int win_h, bool fullscreen) {
  if (!this->menu_list || !this->font) return;
  // Bar is fully hidden — nothing to draw
  if (this->y_offset <= -(float)MENUBAR_HEIGHT) return;

  TTF_SetFontSize(this->font, 16);
  TTF_SetFontStyle(this->font, TTF_STYLE_NORMAL);
  this->rebuild_title_layouts(win_w);

  this->draw_bar_strip(r, win_w);
  this->draw_dropdown(r, win_w, win_h);
}

// ── Event handling ────────────────────────────────────────────────────────────

void SDLMenuBar::dispatch_item(int menu_idx, int item_idx) {
  auto menus = this->get_top_menus();
  if (menu_idx < 0 || menu_idx >= (int)menus.size()) return;
  const auto& menu = menus[menu_idx];
  if (item_idx < 0 || item_idx >= (int)menu->items.size()) return;
  const auto& item = menu->items[item_idx];
  if (!item.enabled) return;
  smb_log.info_f("Dispatching menu {} item {} ({})", menu->menu_id, item_idx + 1, item.name);
  PushMenuEvent(menu->menu_id, (int16_t)(item_idx + 1));
}

bool SDLMenuBar::handle_event(const SDL_Event& e, bool fullscreen, int win_w, int win_h) {
  if (!this->menu_list || !this->font) return false;
  // In fullscreen: ignore events when bar is fully hidden and not being triggered
  if (fullscreen && this->y_offset <= -(float)MENUBAR_HEIGHT) {
    if (e.type != SDL_EVENT_MOUSE_MOTION && e.type != SDL_EVENT_KEY_DOWN) return false;
  }

  this->rebuild_title_layouts(win_w);

  switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION: {
      float px = e.motion.x;
      float py = e.motion.y;
      int bar_idx = this->hit_test_bar(px, py);
      if (this->open_menu_idx >= 0 && bar_idx >= 0 && bar_idx != this->open_menu_idx) {
        // Slide to adjacent menu
        this->open_menu_idx = bar_idx;
        this->hovered_item_idx = -1;
      }
      if (this->open_menu_idx >= 0) {
        this->hovered_item_idx = this->hit_test_dropdown(px, py, win_w, win_h);
      }
      // Motion events don't block the game
      return false;
    }

    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
      if (e.button.button != SDL_BUTTON_LEFT) return false;
      float px = e.button.x;
      float py = e.button.y;

      int bar_idx = this->hit_test_bar(px, py);
      if (bar_idx >= 0) {
        if (this->open_menu_idx == bar_idx) {
          this->open_menu_idx = -1; // toggle closed
          this->hovered_item_idx = -1;
        } else {
          this->open_menu_idx = bar_idx;
          this->hovered_item_idx = -1;
        }
        return true;
      }

      if (this->open_menu_idx >= 0) {
        int item_idx = this->hit_test_dropdown(px, py, win_w, win_h);
        if (item_idx >= 0) {
          this->dispatch_item(this->open_menu_idx, item_idx);
          this->open_menu_idx = -1;
          this->hovered_item_idx = -1;
          return true;
        }
        // Click outside dropdown: close it, let game see the click
        this->open_menu_idx = -1;
        this->hovered_item_idx = -1;
        return false;
      }
      return false;
    }

    case SDL_EVENT_KEY_DOWN: {
      // Escape closes open menu
      if (e.key.key == SDLK_ESCAPE && this->open_menu_idx >= 0) {
        this->open_menu_idx = -1;
        this->hovered_item_idx = -1;
        return true;
      }

      // Ctrl+letter or Cmd+letter → keyboard shortcut
      bool ctrl = (e.key.mod & SDL_KMOD_CTRL) != 0;
      bool gui = (e.key.mod & SDL_KMOD_GUI) != 0;
      if (ctrl || gui) {
        SDL_Keycode sym = e.key.key;
        // SDL3 keycodes for letter keys are their lowercase ASCII values
        char ch = 0;
        if (sym >= SDLK_A && sym <= SDLK_Z) {
          ch = (char)(sym - SDLK_A + 'A'); // normalize to uppercase
        } else if (sym >= SDLK_0 && sym <= SDLK_9) {
          ch = (char)(sym - SDLK_0 + '0');
        }
        if (ch) {
          int32_t result = MenuManager_FindItemByKeyEquivalent(ch);
          if (result != 0) {
            int16_t mid = (int16_t)(result >> 16);
            int16_t iid = (int16_t)(result & 0xFFFF);
            smb_log.info_f("Keyboard shortcut: menu {} item {} (0-based {})", mid, iid + 1, iid);
            PushMenuEvent(mid, (int16_t)(iid + 1));
            return true;
          }
        }
      }
      return false;
    }

    default:
      return false;
  }
}
