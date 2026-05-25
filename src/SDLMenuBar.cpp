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
#include <format>

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

static void draw_frect_outline(SDL_Renderer* r, float x, float y, float w, float h) {
  SDL_FRect fr{x, y, w, h};
  SDL_RenderRect(r, &fr);
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
  this->destroy_text_cache();
  this->open_menu_idx = -1;
  this->hovered_item_idx = -1;
  this->submenu_open_item_idx = -1;
  this->submenu_hovered_item_idx = -1;
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
  this->submenu_open_item_idx = -1;
  this->submenu_hovered_item_idx = -1;
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

std::shared_ptr<Menu> SDLMenuBar::find_submenu(int16_t menu_id) const {
  if (!this->menu_list) return nullptr;
  for (const auto& sm : this->menu_list->submenus) {
    if (sm->menu_id == menu_id) return sm;
  }
  // Also check top-level menus — Speed/Sound are top-level but also referenced as submenus
  for (const auto& m : this->menu_list->menus) {
    if (m->menu_id == menu_id) return m;
  }
  return nullptr;
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
    if (!is_submenu(item)) {
      std::string sc = shortcut_label(item);
      if (!sc.empty()) iw = std::max(iw, ITEM_LPAD + this->measure_text_width(item.name) + 8 + this->measure_text_width(sc) + ITEM_RPAD / 2);
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

int SDLMenuBar::submenu_panel_x(int parent_dx, int parent_dw, int submenu_w, int win_w) const {
  int x = parent_dx + parent_dw - 1;
  if (x + submenu_w > win_w) x = parent_dx - submenu_w + 1;
  if (x < 0) x = 0;
  return x;
}

int SDLMenuBar::submenu_panel_y(int parent_drop_y, int item_pos_in_panel, int win_h) const {
  // item_pos_in_panel: pixel offset of the item from the top of the dropdown content (after 2px padding)
  int y = parent_drop_y + 2 + item_pos_in_panel;
  return y;
}

// Helper: compute pixel y-offset of item i within a dropdown's content area (below the 2px top padding)
static int item_y_in_dropdown(const std::shared_ptr<Menu>& menu, int item_idx) {
  int y = 0;
  for (int i = 0; i < item_idx && i < (int)menu->items.size(); i++) {
    y += is_separator(menu->items[i]) ? SDLMenuBar::SEP_H : SDLMenuBar::ITEM_H;
  }
  return y;
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
  float drop_y = bt + MENUBAR_HEIGHT;

  if (py < drop_y || py >= drop_y + this->dropdown_height(this->open_menu_idx)) return -1;
  if (px < dx || px >= dx + dw) return -1;

  auto menus = this->get_top_menus();
  if (this->open_menu_idx >= (int)menus.size()) return -1;
  const auto& menu = menus[this->open_menu_idx];
  float iy = drop_y + 2.0f;
  for (int i = 0; i < (int)menu->items.size(); i++) {
    float item_h = is_separator(menu->items[i]) ? (float)SEP_H : (float)ITEM_H;
    if (py >= iy && py < iy + item_h) {
      return is_separator(menu->items[i]) ? -1 : i;
    }
    iy += item_h;
  }
  return -1;
}

// Hit test inside the submenu cascade panel. Returns submenu item index, or -1.
static int hit_test_submenu_panel(
    float px, float py,
    const std::shared_ptr<Menu>& submenu,
    int panel_x, int panel_y, int panel_w) {
  if (!submenu) return -1;
  int h = 4;
  for (const auto& item : submenu->items) h += is_separator(item) ? SDLMenuBar::SEP_H : SDLMenuBar::ITEM_H;
  if (px < panel_x || px >= panel_x + panel_w) return -1;
  if (py < panel_y || py >= panel_y + h) return -1;
  float iy = (float)(panel_y + 2);
  for (int i = 0; i < (int)submenu->items.size(); i++) {
    float ih = is_separator(submenu->items[i]) ? (float)SDLMenuBar::SEP_H : (float)SDLMenuBar::ITEM_H;
    if (py >= iy && py < iy + ih) return is_separator(submenu->items[i]) ? -1 : i;
    iy += ih;
  }
  return -1;
}

// ── Text cache helpers ────────────────────────────────────────────────────────

static std::string text_cache_key(const std::string& text, SDL_Color color) {
  char suffix[5];
  suffix[0] = '\xFF'; // sentinel
  suffix[1] = color.r;
  suffix[2] = color.g;
  suffix[3] = color.b;
  suffix[4] = '\0';
  return text + suffix;
}

// ── Drawing ───────────────────────────────────────────────────────────────────

void SDLMenuBar::draw_text(SDL_Renderer* r, const std::string& text, int x, int y, SDL_Color color) const {
  if (!this->font || text.empty()) return;

  // Cache key: text + color bytes
  std::string key = text_cache_key(text, color);
  SDL_Texture* tex = nullptr;

  auto it = this->text_cache.find(key);
  if (it != this->text_cache.end()) {
    tex = it->second;
  } else {
    SDL_Surface* surf = TTF_RenderText_Blended(this->font, text.c_str(), text.size(), color);
    if (!surf) return;
    tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_DestroySurface(surf);
    if (!tex) return;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    const_cast<SDLMenuBar*>(this)->text_cache[key] = tex;
  }

  float tw = 0, th = 0;
  SDL_GetTextureSize(tex, &tw, &th);
  SDL_FRect dst{(float)x, (float)y, tw, th};
  SDL_RenderTexture(r, tex, nullptr, &dst);
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
    this->destroy_text_cache();
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

void SDLMenuBar::destroy_text_cache() {
  for (auto& [key, tex] : this->text_cache) {
    if (tex) SDL_DestroyTexture(tex);
  }
  this->text_cache.clear();
}

// Draw a dropdown panel for 'menu' at (panel_x, panel_y), width panel_w.
// highlight_item: index of the highlighted item inside this panel (-1 = none).
// Returns the pixel height of the drawn panel.
int SDLMenuBar::draw_panel(
    SDL_Renderer* r,
    const std::shared_ptr<Menu>& menu,
    float panel_x, float panel_y, int panel_w,
    int highlight_item,
    int win_w, int win_h) {

  int dh = 4;
  for (const auto& item : menu->items) dh += is_separator(item) ? SDLMenuBar::SEP_H : SDLMenuBar::ITEM_H;

  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
  set_draw_color(r, COLOR_SHADOW);
  fill_frect(r, panel_x + 3, panel_y + 3, (float)panel_w, (float)dh);

  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
  set_draw_color(r, COLOR_DROP_BG);
  fill_frect(r, panel_x, panel_y, (float)panel_w, (float)dh);
  set_draw_color(r, COLOR_DROP_BDR);
  draw_frect_outline(r, panel_x, panel_y, (float)panel_w, (float)dh);

  TTF_SetFontSize(this->font, 16);
  TTF_SetFontStyle(this->font, TTF_STYLE_NORMAL);

  float iy = panel_y + 2.0f;
  for (int i = 0; i < (int)menu->items.size(); i++) {
    const auto& item = menu->items[i];
    if (is_separator(item)) {
      SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
      set_draw_color(r, COLOR_SEP);
      SDL_RenderLine(r, (int)panel_x + 4, (int)(iy + SDLMenuBar::SEP_H / 2), (int)panel_x + panel_w - 4, (int)(iy + SDLMenuBar::SEP_H / 2));
      iy += SDLMenuBar::SEP_H;
      continue;
    }

    bool hovered = (i == highlight_item && item.enabled);
    if (hovered) {
      SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
      set_draw_color(r, COLOR_ITEM_HL);
      fill_frect(r, panel_x + 1, iy, (float)(panel_w - 2), (float)SDLMenuBar::ITEM_H);
    }

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Color text_col = item.enabled ? COLOR_WHITE : COLOR_GRAY;
    int text_y = (int)(iy + (SDLMenuBar::ITEM_H - 16) / 2.0f);

    if (item.checked) {
      this->draw_text(r, "\xe2\x9c\x93", (int)panel_x + 5, text_y, text_col);
    }

    if (item.icon_image) {
      SDL_Texture* tex = this->get_icon_texture(r, item.icon_image.get());
      if (tex) {
        SDL_FRect icon_dst{panel_x + 13, iy + (SDLMenuBar::ITEM_H - 16) / 2.0f, 16.0f, 16.0f};
        SDL_RenderTexture(r, tex, nullptr, &icon_dst);
      }
    }

    this->draw_text(r, item.name, (int)panel_x + SDLMenuBar::ITEM_LPAD, text_y, text_col);

    if (is_submenu(item)) {
      this->draw_text(r, "\xe2\x96\xb6", (int)panel_x + panel_w - 14, text_y, text_col);
    } else {
      std::string sc = shortcut_label(item);
      if (!sc.empty()) {
        int sw = this->measure_text_width(sc);
        this->draw_text(r, sc, (int)panel_x + panel_w - SDLMenuBar::ITEM_RPAD / 2 - sw / 2, text_y, COLOR_SHORTCUT);
      }
    }

    iy += SDLMenuBar::ITEM_H;
  }

  return dh;
}

void SDLMenuBar::draw_bar_strip(SDL_Renderer* r, int win_w) {
  float bt = this->y_offset;
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

  set_draw_color(r, COLOR_BAR_BG);
  fill_frect(r, 0, bt, (float)win_w, (float)MENUBAR_HEIGHT);
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
  float drop_y = bt + MENUBAR_HEIGHT;

  this->draw_panel(r, menu, (float)dx, drop_y, dw, this->hovered_item_idx, win_w, win_h);

  // Draw submenu cascade panel if an item with a submenu is hovered
  if (this->submenu_open_item_idx >= 0 && this->submenu_open_item_idx < (int)menu->items.size()) {
    const auto& sub_item = menu->items[this->submenu_open_item_idx];
    if (is_submenu(sub_item)) {
      int16_t sub_id = (int16_t)(unsigned char)sub_item.mark_character;
      auto submenu = this->find_submenu(sub_id);
      if (submenu && !submenu->items.empty()) {
        int sub_w = DROPDOWN_MIN_W;
        for (const auto& it : submenu->items) {
          if (!is_separator(it)) {
            int iw = ITEM_LPAD + this->measure_text_width(it.name) + ITEM_RPAD;
            sub_w = std::max(sub_w, iw);
          }
        }
        int item_y_off = item_y_in_dropdown(menu, this->submenu_open_item_idx);
        int sub_x = this->submenu_panel_x(dx, dw, sub_w, win_w);
        int sub_y = this->submenu_panel_y((int)drop_y, item_y_off, win_h);
        this->draw_panel(r, submenu, (float)sub_x, (float)sub_y, sub_w, this->submenu_hovered_item_idx, win_w, win_h);
      }
    }
  }
}

void SDLMenuBar::draw(SDL_Renderer* r, int win_w, int win_h, bool fullscreen) {
  if (!this->menu_list || !this->font) return;
  if (this->y_offset <= -(float)MENUBAR_HEIGHT) return;

  // Invalidate text cache if renderer changed
  if (this->cached_renderer != r) {
    this->destroy_icon_cache();
    this->destroy_text_cache();
    this->cached_renderer = r;
  }

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
  if (!item.enabled || is_separator(item)) return;
  smb_log.info_f("Dispatching menu {} item {} ({})", menu->menu_id, item_idx + 1, item.name);
  PushMenuEvent(menu->menu_id, (int16_t)(item_idx + 1));
}

void SDLMenuBar::dispatch_submenu_item(int menu_idx, int submenu_item_idx, int item_idx) {
  auto menus = this->get_top_menus();
  if (menu_idx < 0 || menu_idx >= (int)menus.size()) return;
  const auto& parent_menu = menus[menu_idx];
  if (submenu_item_idx < 0 || submenu_item_idx >= (int)parent_menu->items.size()) return;
  const auto& sub_item = parent_menu->items[submenu_item_idx];
  if (!is_submenu(sub_item)) return;
  int16_t sub_id = (int16_t)(unsigned char)sub_item.mark_character;
  auto submenu = this->find_submenu(sub_id);
  if (!submenu) return;
  if (item_idx < 0 || item_idx >= (int)submenu->items.size()) return;
  const auto& item = submenu->items[item_idx];
  if (!item.enabled || is_separator(item)) return;
  smb_log.info_f("Dispatching submenu {} item {} ({})", submenu->menu_id, item_idx + 1, item.name);
  PushMenuEvent(submenu->menu_id, (int16_t)(item_idx + 1));
}

bool SDLMenuBar::handle_event(const SDL_Event& e, bool fullscreen, int win_w, int win_h) {
  if (!this->menu_list || !this->font) return false;
  if (fullscreen && this->y_offset <= -(float)MENUBAR_HEIGHT) {
    if (e.type != SDL_EVENT_MOUSE_MOTION && e.type != SDL_EVENT_KEY_DOWN) return false;
  }

  this->rebuild_title_layouts(win_w);

  switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION: {
      float px = e.motion.x;
      float py = e.motion.y;

      // Check if cursor is over a submenu panel
      if (this->open_menu_idx >= 0 && this->submenu_open_item_idx >= 0) {
        auto menus = this->get_top_menus();
        if (this->open_menu_idx < (int)menus.size()) {
          const auto& menu = menus[this->open_menu_idx];
          if (this->submenu_open_item_idx < (int)menu->items.size()) {
            const auto& sub_item = menu->items[this->submenu_open_item_idx];
            if (is_submenu(sub_item)) {
              int16_t sub_id = (int16_t)(unsigned char)sub_item.mark_character;
              auto submenu = this->find_submenu(sub_id);
              if (submenu) {
                int dx = this->dropdown_x(this->open_menu_idx, win_w);
                int dw = this->dropdown_width(this->open_menu_idx);
                int sub_w = DROPDOWN_MIN_W;
                for (const auto& it : submenu->items) {
                  if (!is_separator(it)) sub_w = std::max(sub_w, ITEM_LPAD + this->measure_text_width(it.name) + ITEM_RPAD);
                }
                int item_y_off = item_y_in_dropdown(menu, this->submenu_open_item_idx);
                int sub_x = this->submenu_panel_x(dx, dw, sub_w, win_w);
                int sub_y = this->submenu_panel_y((int)(this->y_offset + MENUBAR_HEIGHT), item_y_off, win_h);
                int hi = hit_test_submenu_panel(px, py, submenu, sub_x, sub_y, sub_w);
                if (hi >= 0) {
                  this->submenu_hovered_item_idx = hi;
                  return false;
                }
              }
            }
          }
        }
      }

      int bar_idx = this->hit_test_bar(px, py);
      if (this->open_menu_idx >= 0 && bar_idx >= 0 && bar_idx != this->open_menu_idx) {
        this->open_menu_idx = bar_idx;
        this->hovered_item_idx = -1;
        this->submenu_open_item_idx = -1;
        this->submenu_hovered_item_idx = -1;
      }
      if (this->open_menu_idx >= 0) {
        int old_hovered = this->hovered_item_idx;
        this->hovered_item_idx = this->hit_test_dropdown(px, py, win_w, win_h);
        if (this->hovered_item_idx != old_hovered) {
          // If hovering a new item, update submenu state
          if (this->hovered_item_idx >= 0) {
            auto menus = this->get_top_menus();
            if (this->open_menu_idx < (int)menus.size()) {
              const auto& menu = menus[this->open_menu_idx];
              if (this->hovered_item_idx < (int)menu->items.size()) {
                const auto& item = menu->items[this->hovered_item_idx];
                if (is_submenu(item)) {
                  this->submenu_open_item_idx = this->hovered_item_idx;
                  this->submenu_hovered_item_idx = -1;
                } else {
                  this->submenu_open_item_idx = -1;
                  this->submenu_hovered_item_idx = -1;
                }
              }
            }
          } else {
            // Not hovering any item in main dropdown — close submenu only if
            // not hovering the submenu panel (handled above)
            this->submenu_open_item_idx = -1;
            this->submenu_hovered_item_idx = -1;
          }
        }
      }
      return false;
    }

    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
      if (e.button.button != SDL_BUTTON_LEFT) return false;
      float px = e.button.x;
      float py = e.button.y;

      // Click in submenu panel?
      if (this->open_menu_idx >= 0 && this->submenu_open_item_idx >= 0) {
        auto menus = this->get_top_menus();
        if (this->open_menu_idx < (int)menus.size()) {
          const auto& menu = menus[this->open_menu_idx];
          if (this->submenu_open_item_idx < (int)menu->items.size()) {
            const auto& sub_item = menu->items[this->submenu_open_item_idx];
            if (is_submenu(sub_item)) {
              int16_t sub_id = (int16_t)(unsigned char)sub_item.mark_character;
              auto submenu = this->find_submenu(sub_id);
              if (submenu) {
                int dx = this->dropdown_x(this->open_menu_idx, win_w);
                int dw = this->dropdown_width(this->open_menu_idx);
                int sub_w = DROPDOWN_MIN_W;
                for (const auto& it : submenu->items) {
                  if (!is_separator(it)) sub_w = std::max(sub_w, ITEM_LPAD + this->measure_text_width(it.name) + ITEM_RPAD);
                }
                int item_y_off = item_y_in_dropdown(menu, this->submenu_open_item_idx);
                int sub_x = this->submenu_panel_x(dx, dw, sub_w, win_w);
                int sub_y = this->submenu_panel_y((int)(this->y_offset + MENUBAR_HEIGHT), item_y_off, win_h);
                int hi = hit_test_submenu_panel(px, py, submenu, sub_x, sub_y, sub_w);
                if (hi >= 0) {
                  this->dispatch_submenu_item(this->open_menu_idx, this->submenu_open_item_idx, hi);
                  this->open_menu_idx = -1;
                  this->hovered_item_idx = -1;
                  this->submenu_open_item_idx = -1;
                  this->submenu_hovered_item_idx = -1;
                  return true;
                }
              }
            }
          }
        }
      }

      int bar_idx = this->hit_test_bar(px, py);
      if (bar_idx >= 0) {
        if (this->open_menu_idx == bar_idx) {
          this->open_menu_idx = -1;
          this->hovered_item_idx = -1;
        } else {
          this->open_menu_idx = bar_idx;
          this->hovered_item_idx = -1;
        }
        this->submenu_open_item_idx = -1;
        this->submenu_hovered_item_idx = -1;
        return true;
      }

      if (this->open_menu_idx >= 0) {
        int item_idx = this->hit_test_dropdown(px, py, win_w, win_h);
        if (item_idx >= 0) {
          // Don't dispatch submenu items on click — hovering opens them
          auto menus = this->get_top_menus();
          bool sub = (this->open_menu_idx < (int)menus.size() &&
                      item_idx < (int)menus[this->open_menu_idx]->items.size() &&
                      is_submenu(menus[this->open_menu_idx]->items[item_idx]));
          if (!sub) {
            this->dispatch_item(this->open_menu_idx, item_idx);
          }
          if (!sub) {
            this->open_menu_idx = -1;
            this->hovered_item_idx = -1;
            this->submenu_open_item_idx = -1;
            this->submenu_hovered_item_idx = -1;
          }
          return true;
        }
        // Click outside: close menu
        this->open_menu_idx = -1;
        this->hovered_item_idx = -1;
        this->submenu_open_item_idx = -1;
        this->submenu_hovered_item_idx = -1;
        return false;
      }
      return false;
    }

    case SDL_EVENT_KEY_DOWN: {
      if (e.key.key == SDLK_ESCAPE && this->open_menu_idx >= 0) {
        if (this->submenu_open_item_idx >= 0) {
          this->submenu_open_item_idx = -1;
          this->submenu_hovered_item_idx = -1;
        } else {
          this->open_menu_idx = -1;
          this->hovered_item_idx = -1;
        }
        return true;
      }

      bool ctrl = (e.key.mod & SDL_KMOD_CTRL) != 0;
      bool gui = (e.key.mod & SDL_KMOD_GUI) != 0;
      if (ctrl || gui) {
        SDL_Keycode sym = e.key.key;
        char ch = 0;
        if (sym >= SDLK_A && sym <= SDLK_Z) {
          ch = (char)(sym - SDLK_A + 'A');
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
