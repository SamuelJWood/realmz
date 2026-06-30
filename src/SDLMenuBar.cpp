#include "SDLMenuBar.hpp"

#include "EventManager.h"
#include "FileManager.hpp"
#include "Font.hpp"
#include "MenuManager-C-Interface.h"
#include "WindowManager.hpp"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <phosg/Strings.hh>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <format>

static phosg::PrefixedLogger smb_log("[SDLMenuBar] ");

Uint32 SDLMenuBar::s_anim_event_type = 0;
Uint32 SDLMenuBar::s_submenu_event_type = 0;
Uint32 SDLMenuBar::s_submenu_close_event_type = 0;

// ── Colors ────────────────────────────────────────────────────────────────────

static constexpr SDL_Color COLOR_BAR_BG    = {0x2D, 0x2D, 0x2D, 0xFF};
static constexpr SDL_Color COLOR_BAR_EDGE  = {0x1A, 0x1A, 0x1A, 0xFF};
static constexpr SDL_Color COLOR_TITLE_HL  = {0x4A, 0x4A, 0x8A, 0xFF};
static constexpr SDL_Color COLOR_DROP_BG   = {0x3D, 0x3D, 0x3D, 0xFF};
static constexpr SDL_Color COLOR_DROP_BDR  = {0x55, 0x55, 0x55, 0xFF};
static constexpr SDL_Color COLOR_SHADOW    = {0x18, 0x18, 0x18, 0x80};
static constexpr SDL_Color COLOR_ITEM_HL   = {0x44, 0x55, 0xCC, 0xFF};
static constexpr SDL_Color COLOR_ITEM_SEL  = {0x2A, 0x42, 0x6A, 0xFF}; // checked/selected item
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

// Draws a diamond (rotated square) centered at (cx, cy) with the given radius
// (tip-to-center distance, in pixels). filled = solid diamond (permanent
// conditions); otherwise a 1px outline (temporary). The menu font has no diamond
// glyph, so we render it geometrically.
//
// Drawn row-by-row on integer pixels so it's symmetric and crisp: at vertical
// offset dy the diamond spans cx-(R-|dy|) .. cx+(R-|dy|), tapering to a single
// pixel at the top and bottom tips. The filled form fills each row; the outline
// form plots just the two edge pixels per row (which meet at all four tips).
static void draw_diamond(SDL_Renderer* r, int cx, int cy, int radius, bool filled, SDL_Color c) {
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
  set_draw_color(r, c);
  for (int dy = -radius; dy <= radius; dy++) {
    int hw = radius - (dy < 0 ? -dy : dy); // half-width of this row
    int y = cy + dy;
    if (filled || hw == 0) {
      SDL_FRect row{(float)(cx - hw), (float)y, (float)(2 * hw + 1), 1.0f};
      SDL_RenderFillRect(r, &row);
    } else {
      SDL_FRect lp{(float)(cx - hw), (float)y, 1.0f, 1.0f};
      SDL_FRect rp{(float)(cx + hw), (float)y, 1.0f, 1.0f};
      SDL_RenderFillRect(r, &lp);
      SDL_RenderFillRect(r, &rp);
    }
  }
}

static constexpr SDL_Color COLOR_PDF_OPEN   = {0x6E, 0x6E, 0xCC, 0xFF}; // "opening" fill (clicked)
static constexpr SDL_Color COLOR_PDF_BDR    = {0xC8, 0xC8, 0xD2, 0xFF}; // hover outline

// Manual-icon button geometry (the image is 32x22; the box adds hover/active padding).
static constexpr float MANUAL_ICON_W = 32.0f;
static constexpr float MANUAL_ICON_H = 22.0f;
static constexpr float PDF_BOX_PAD = 3.0f;

// Screen rect of the manual-icon button (the box drawn on hover/click) within a
// dropdown item's shortcut column.
static SDL_FRect pdf_button_rect(float panel_x, int panel_w, float item_iy, float item_h) {
  float w = MANUAL_ICON_W + 2 * PDF_BOX_PAD;
  float h = MANUAL_ICON_H + 2 * PDF_BOX_PAD;
  float cx = panel_x + (float)panel_w - (float)SDLMenuBar::ITEM_RPAD / 2.0f;
  return SDL_FRect{cx - w / 2.0f, item_iy + (item_h - h) / 2.0f, w, h};
}

// Open a local PDF in the OS default viewer via a percent-encoded file:// URL.
static void open_pdf_file(const std::string& host_path) {
  if (host_path.empty()) return;
  std::string p = host_path;
  for (auto& c : p) {
    if (c == '\\') c = '/';
  }
  std::string url = "file://";
  if (!p.empty() && p[0] != '/') url += '/'; // Windows "C:/..." needs the extra slash
  for (unsigned char c : p) {
    if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == ':' || c == '~') {
      url += static_cast<char>(c);
    } else {
      char b[8];
      SDL_snprintf(b, sizeof b, "%%%02X", c);
      url += b;
    }
  }
  SDL_OpenURL(url.c_str());
}

static bool is_separator(const Menu::Item& item) {
  return item.name == "-" || item.name.empty();
}

static bool is_submenu(const Menu::Item& item) {
  return item.key_equivalent == '\x1B';
}

static std::string shortcut_label(const Menu::Item& item) {
  if (!item.shortcut_text.empty()) return item.shortcut_text;
  if (!item.key_equivalent || is_submenu(item)) return "";
  char buf[16];
  snprintf(buf, sizeof(buf), "Ctrl+%c", (char)toupper((unsigned char)item.key_equivalent));
  return buf;
}

// Returns the rendered pixel height for a single non-separator item.
static int item_height(const Menu::Item& item) {
  return item.icon_image ? SDLMenuBar::ICON_ITEM_H : SDLMenuBar::ITEM_H;
}

// ── Singleton ─────────────────────────────────────────────────────────────────

SDLMenuBar& SDLMenuBar::instance() {
  static SDLMenuBar inst;
  return inst;
}

// ── Public interface ──────────────────────────────────────────────────────────

void SDLMenuBar::init(TTF_Font* f) {
  this->font = f;
  if (s_anim_event_type == 0) {
    s_anim_event_type = SDL_RegisterEvents(1);
  }
  if (s_submenu_event_type == 0) {
    s_submenu_event_type = SDL_RegisterEvents(1);
  }
  if (s_submenu_close_event_type == 0) {
    s_submenu_close_event_type = SDL_RegisterEvents(1);
  }
}

void SDLMenuBar::sync(std::shared_ptr<MenuList> ml) {
  this->menu_list = ml;
  this->cached_menus.assign(ml->menus.begin(), ml->menus.end());
  this->title_layout_win_w = 0; // force rebuild
  this->destroy_icon_cache();
  this->destroy_text_cache(); // also clears text_width_cache
  this->open_menu_idx = -1;
  this->hovered_item_idx = -1;
  this->submenu_open_item_idx = -1;
  this->submenu_hovered_item_idx = -1;
  if (this->submenu_timer_id) { SDL_RemoveTimer(this->submenu_timer_id); this->submenu_timer_id = 0; }
  if (this->submenu_close_timer_id) { SDL_RemoveTimer(this->submenu_close_timer_id); this->submenu_close_timer_id = 0; }
  this->submenu_pending_item_idx = -1;
  this->dropdown_scroll_px = 0.0f;
  this->scroll_up_active = false;
  this->scroll_down_active = false;
  this->kbd_focused_idx = -1;
  this->kbd_in_dropdown = false;
  this->kbd_in_submenu = false;
  this->alt_key_pending = false;
}

int SDLMenuBar::reserved_top_pixels(bool fullscreen) {
  return fullscreen ? 0 : MENUBAR_HEIGHT;
}

// ── Animation ─────────────────────────────────────────────────────────────────

void SDLMenuBar::on_fullscreen_changed(bool now_fullscreen) {
  this->y_offset = 0.0f;
  this->y_target = 0.0f;
  this->open_menu_idx = -1;
  this->hovered_item_idx = -1;
  this->submenu_open_item_idx = -1;
  this->submenu_hovered_item_idx = -1;
  if (this->submenu_timer_id) { SDL_RemoveTimer(this->submenu_timer_id); this->submenu_timer_id = 0; }
  if (this->submenu_close_timer_id) { SDL_RemoveTimer(this->submenu_close_timer_id); this->submenu_close_timer_id = 0; }
  this->submenu_pending_item_idx = -1;
  this->dropdown_scroll_px = 0.0f;
  this->scroll_up_active = false;
  this->scroll_down_active = false;
  this->auto_hide_timer = 0.0f;
  this->kbd_focused_idx = -1;
  this->kbd_in_dropdown = false;
  this->kbd_in_submenu = false;
  this->alt_key_pending = false;
}

void SDLMenuBar::update(float dt, int cursor_y, bool fullscreen, int win_h) {
  // Forced hidden (e.g. during the logo splash): keep the bar fully retracted.
  if (this->force_hidden) {
    this->y_offset = -(float)MENUBAR_HEIGHT;
    this->y_target = -(float)MENUBAR_HEIGHT;
    return;
  }

  // Menu bar is always visible — no auto-hide in fullscreen.
  this->y_offset = 0.0f;
  this->y_target = 0.0f;

  // Drive dropdown scroll animation when cursor is in an arrow zone.
  if (this->open_menu_idx >= 0 && (this->scroll_up_active || this->scroll_down_active)) {
    float drop_y = this->bar_top() + (float)MENUBAR_HEIGHT;
    int natural_h = this->dropdown_height(this->open_menu_idx);
    int vis_h = this->dropdown_visible_h(drop_y, natural_h, win_h);
    int max_scroll = natural_h - vis_h;
    if (max_scroll > 0) {
      float delta = SCROLL_SPEED_PPS * dt;
      if (this->scroll_down_active)
        this->dropdown_scroll_px = std::min(this->dropdown_scroll_px + delta, (float)max_scroll);
      else
        this->dropdown_scroll_px = std::max(this->dropdown_scroll_px - delta, 0.0f);
    }
    if (!this->anim_event_pending && s_anim_event_type != 0) {
      SDL_Event ev{};
      ev.type = s_anim_event_type;
      SDL_PushEvent(&ev);
      this->anim_event_pending = true;
    }
  }
}

void SDLMenuBar::set_force_hidden(bool hidden) {
  if (this->force_hidden == hidden) return;
  this->force_hidden = hidden;
  if (hidden) {
    // Dismiss any open dropdown/submenu so nothing lingers over the splash.
    this->close_all();
    this->y_offset = -(float)MENUBAR_HEIGHT;
    this->y_target = -(float)MENUBAR_HEIGHT;
  } else {
    this->y_offset = 0.0f;
    this->y_target = 0.0f;
  }
}

// ── Layout ────────────────────────────────────────────────────────────────────

const std::vector<std::shared_ptr<Menu>>& SDLMenuBar::get_top_menus() const {
  return this->cached_menus;
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
    int lpad = item.icon_image ? ICON_ITEM_LPAD : ITEM_LPAD;
    int iw = lpad + this->measure_text_width(item.name) + ITEM_RPAD;
    if (!is_submenu(item)) {
      std::string sc = shortcut_label(item);
      if (!sc.empty()) iw = std::max(iw, lpad + this->measure_text_width(item.name) + 8 + this->measure_text_width(sc) + ITEM_RPAD / 2);
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
    h += is_separator(item) ? SEP_H : item_height(item);
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
    y += is_separator(menu->items[i]) ? SDLMenuBar::SEP_H : item_height(menu->items[i]);
  }
  return y;
}

int SDLMenuBar::dropdown_visible_h(float drop_y, int natural_h, int win_h) const {
  int available = win_h - (int)drop_y - 4;
  int min_h = SCROLL_ARROW_H * 2 + ITEM_H;
  return std::min(natural_h, std::max(available, min_h));
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
  float drop_y = bt + (float)MENUBAR_HEIGHT;

  int natural_h = this->dropdown_height(this->open_menu_idx);
  int vis_h = this->dropdown_visible_h(drop_y, natural_h, win_h);
  bool scrollable = (natural_h > vis_h);
  int scroll_px = scrollable ? (int)this->dropdown_scroll_px : 0;

  if (py < drop_y || py >= drop_y + (float)vis_h) return -1;
  if (px < dx || px >= dx + dw) return -1;

  // Exclude arrow zones from item hit-testing.
  if (scrollable) {
    if (scroll_px > 0 && py < drop_y + SCROLL_ARROW_H) return -1;
    if (scroll_px < natural_h - vis_h && py >= drop_y + (float)vis_h - SCROLL_ARROW_H) return -1;
  }

  auto menus = this->get_top_menus();
  if (this->open_menu_idx >= (int)menus.size()) return -1;
  const auto& menu = menus[this->open_menu_idx];
  float iy = drop_y + 2.0f - (float)scroll_px;
  for (int i = 0; i < (int)menu->items.size(); i++) {
    float item_h = is_separator(menu->items[i]) ? (float)SEP_H : (float)item_height(menu->items[i]);
    if (py >= iy && py < iy + item_h) {
      return is_separator(menu->items[i]) ? -1 : i;
    }
    iy += item_h;
  }
  return -1;
}

int SDLMenuBar::hit_test_dropdown_pdf(float px, float py, int win_w, int win_h) const {
  if (this->open_menu_idx < 0) return -1;
  float bt = this->y_offset;
  float dx = (float)this->dropdown_x(this->open_menu_idx, win_w);
  float dw = (float)this->dropdown_width(this->open_menu_idx);
  float drop_y = bt + (float)MENUBAR_HEIGHT;

  int natural_h = this->dropdown_height(this->open_menu_idx);
  int vis_h = this->dropdown_visible_h(drop_y, natural_h, win_h);
  bool scrollable = (natural_h > vis_h);
  int scroll_px = scrollable ? (int)this->dropdown_scroll_px : 0;

  if (py < drop_y || py >= drop_y + (float)vis_h) return -1;
  if (px < dx || px >= dx + dw) return -1;
  if (scrollable) {
    if (scroll_px > 0 && py < drop_y + SCROLL_ARROW_H) return -1;
    if (scroll_px < natural_h - vis_h && py >= drop_y + (float)vis_h - SCROLL_ARROW_H) return -1;
  }

  auto menus = this->get_top_menus();
  if (this->open_menu_idx >= (int)menus.size()) return -1;
  const auto& menu = menus[this->open_menu_idx];
  float iy = drop_y + 2.0f - (float)scroll_px;
  for (int i = 0; i < (int)menu->items.size(); i++) {
    const auto& it = menu->items[i];
    float item_h = is_separator(it) ? (float)SEP_H : (float)item_height(it);
    if (!is_separator(it) && !it.pdf_path.empty() && !it.opens_pdf_on_click) {
      SDL_FRect br = pdf_button_rect(dx, (int)dw, iy, item_h);
      if (px >= br.x && px < br.x + br.w && py >= br.y && py < br.y + br.h) {
        return i;
      }
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
  auto it = this->text_width_cache.find(text);
  if (it != this->text_width_cache.end()) return it->second;
  int w = 0, h = 0;
  TTF_GetStringSize(this->font, text.c_str(), text.size(), &w, &h);
  const_cast<SDLMenuBar*>(this)->text_width_cache[text] = w;
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

SDL_Texture* SDLMenuBar::get_manual_icon_texture(SDL_Renderer* r) {
  if (this->cached_renderer != r) {
    this->destroy_icon_cache();
    this->destroy_text_cache();
    this->cached_renderer = r;
  }
  if (this->manual_icon_tex) return this->manual_icon_tex;
  std::string host = host_filename_for_mac_filename(":Data Files:manual_icon.png", false);
  SDL_Surface* surf = IMG_Load(host.c_str());
  if (!surf) return nullptr;
  SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
  SDL_DestroySurface(surf);
  if (!tex) return nullptr;
  SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
  this->manual_icon_tex = tex;
  return tex;
}

void SDLMenuBar::destroy_icon_cache() {
  for (auto& [key, tex] : this->icon_cache) {
    if (tex) SDL_DestroyTexture(tex);
  }
  this->icon_cache.clear();
  if (this->manual_icon_tex) {
    SDL_DestroyTexture(this->manual_icon_tex);
    this->manual_icon_tex = nullptr;
  }
}

void SDLMenuBar::destroy_text_cache() {
  for (auto& [key, tex] : this->text_cache) {
    if (tex) SDL_DestroyTexture(tex);
  }
  this->text_cache.clear();
  this->text_width_cache.clear();
}

// Draw a dropdown panel for 'menu' at (panel_x, panel_y), width panel_w.
// highlight_item: index of the highlighted item inside this panel (-1 = none).
// Returns the pixel height of the drawn panel.
int SDLMenuBar::draw_panel(
    SDL_Renderer* r,
    const std::shared_ptr<Menu>& menu,
    float panel_x, float panel_y, int panel_w,
    int highlight_item,
    int win_w, int win_h,
    int scroll_px, int visible_h,
    bool arrow_up_hovered, bool arrow_down_hovered) {

  int natural_h = 4;
  for (const auto& item : menu->items) natural_h += is_separator(item) ? SDLMenuBar::SEP_H : item_height(item);

  bool scrolling = (visible_h > 0 && natural_h > visible_h);
  int draw_h = scrolling ? visible_h : natural_h;

  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
  set_draw_color(r, COLOR_SHADOW);
  fill_frect(r, panel_x + 3, panel_y + 3, (float)panel_w, (float)draw_h);

  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
  set_draw_color(r, COLOR_DROP_BG);
  fill_frect(r, panel_x, panel_y, (float)panel_w, (float)draw_h);
  set_draw_color(r, COLOR_DROP_BDR);
  draw_frect_outline(r, panel_x, panel_y, (float)panel_w, (float)draw_h);

  // Pre-compute arrow presence so the clip rect can exclude them and the border.
  int max_scroll = scrolling ? (natural_h - visible_h) : 0;
  bool can_up   = scrolling && (scroll_px > 0);
  bool can_down = scrolling && (scroll_px < max_scroll);

  if (scrolling) {
    int clip_x  = (int)panel_x + 1;
    int clip_y  = (int)panel_y + 1 + (can_up   ? SCROLL_ARROW_H : 0);
    int clip_w  = panel_w - 2;
    int clip_h  = draw_h  - 2 - (can_up ? SCROLL_ARROW_H : 0) - (can_down ? SCROLL_ARROW_H : 0);
    if (clip_h < 0) clip_h = 0;
    SDL_Rect clip = {clip_x, clip_y, clip_w, clip_h};
    SDL_SetRenderClipRect(r, &clip);
  }

  TTF_SetFontSize(this->font, 16);
  TTF_SetFontStyle(this->font, TTF_STYLE_NORMAL);

  float iy = panel_y + 2.0f - (float)scroll_px;
  for (int i = 0; i < (int)menu->items.size(); i++) {
    const auto& item = menu->items[i];
    float item_h_f = is_separator(item) ? (float)SDLMenuBar::SEP_H : (float)item_height(item);
    // Skip items entirely outside the visible region.
    if (iy + item_h_f <= panel_y || iy >= panel_y + (float)draw_h) {
      iy += item_h_f;
      continue;
    }

    if (is_separator(item)) {
      SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
      set_draw_color(r, COLOR_SEP);
      SDL_RenderLine(r, (int)panel_x + 4, (int)(iy + SDLMenuBar::SEP_H / 2), (int)panel_x + panel_w - 4, (int)(iy + SDLMenuBar::SEP_H / 2));
      iy += SDLMenuBar::SEP_H;
      continue;
    }

    if (item.is_header) {
      // Header: inverted colors — white background, dark text; never highlighted.
      SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
      set_draw_color(r, COLOR_WHITE);
      fill_frect(r, panel_x + 1, iy, (float)(panel_w - 2), item_h_f);
    } else {
      bool hovered = (i == highlight_item && item.enabled);
      if (hovered) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        set_draw_color(r, COLOR_ITEM_HL);
        fill_frect(r, panel_x + 1, iy, (float)(panel_w - 2), item_h_f);
      } else if (item.checked) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        set_draw_color(r, COLOR_ITEM_SEL);
        fill_frect(r, panel_x + 1, iy, (float)(panel_w - 2), item_h_f);
      }
      // 1px black border around the selected (checked) item, even when hovered.
      if (item.checked) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        draw_frect_outline(r, panel_x + 1, iy, (float)(panel_w - 2), item_h_f);
      }
    }

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    // Header: text color = panel background (dark). Normal: white.
    SDL_Color text_col = item.is_header ? COLOR_DROP_BG
                       : (item.enabled  ? COLOR_WHITE : COLOR_GRAY);
    int text_y = (int)(iy + (item_h_f - 16) / 2.0f);

    if (item.icon_image) {
      SDL_Texture* tex = this->get_icon_texture(r, item.icon_image.get());
      if (tex) {
        // Draw the icon at its native pixel size (preserving aspect ratio),
        // downscaling only if it exceeds the ICON_SIZE box. This keeps non-square
        // icons like the 32x22 manual icon undistorted, while square scenario
        // icons still fill the 32x32 box.
        float iw = (float)item.icon_image->get_width();
        float ih = (float)item.icon_image->get_height();
        float scale = std::min(1.0f, std::min((float)SDLMenuBar::ICON_SIZE / iw,
                                              (float)SDLMenuBar::ICON_SIZE / ih));
        float dw = iw * scale, dh = ih * scale;
        float icon_x = panel_x + 4 + ((float)SDLMenuBar::ICON_SIZE - dw) / 2.0f;
        float icon_y = iy + (item_h_f - dh) / 2.0f;
        SDL_FRect icon_dst{icon_x, icon_y, dw, dh};
        SDL_RenderTexture(r, tex, nullptr, &icon_dst);
      }
    }

    // Diamond mark (Conditions menu): drawn in the left mark column. Filled for
    // permanent conditions, hollow for temporary ones. Use the item's text color
    // so it stays visible on both the normal and hovered backgrounds.
    if (item.mark_glyph != Menu::Item::MARK_NONE) {
      int cx = (int)(panel_x + SDLMenuBar::ITEM_LPAD / 2.0f);
      int cy = (int)(iy + item_h_f / 2.0f);
      draw_diamond(r, cx, cy, 5, item.mark_glyph == Menu::Item::MARK_FILLED_DIAMOND, text_col);
    }

    int text_lpad = item.icon_image ? SDLMenuBar::ICON_ITEM_LPAD : SDLMenuBar::ITEM_LPAD;
    this->draw_text(r, item.name, (int)panel_x + text_lpad, text_y, text_col);

    if (is_submenu(item)) {
      this->draw_text(r, "\xe2\x96\xb6", (int)panel_x + panel_w - 14, text_y, text_col);
    } else if (!item.pdf_path.empty() && !item.opens_pdf_on_click) {
      // Manual-icon button in the shortcut column: a rectangle appears on hover,
      // and fills with the "opening" color while its PDF is being launched.
      SDL_FRect box = pdf_button_rect(panel_x, panel_w, iy, item_h_f);
      if (i == this->pdf_opening_item_idx) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        set_draw_color(r, COLOR_PDF_OPEN);
        SDL_RenderFillRect(r, &box);
      } else if (i == this->hovered_pdf_item_idx) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        set_draw_color(r, COLOR_PDF_BDR);
        SDL_RenderRect(r, &box);
      }
      SDL_Texture* mt = this->get_manual_icon_texture(r);
      if (mt) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_FRect dst{box.x + PDF_BOX_PAD, box.y + PDF_BOX_PAD, MANUAL_ICON_W, MANUAL_ICON_H};
        SDL_RenderTexture(r, mt, nullptr, &dst);
      }
    } else {
      std::string sc = shortcut_label(item);
      if (!sc.empty()) {
        int sw = this->measure_text_width(sc);
        this->draw_text(r, sc, (int)panel_x + panel_w - SDLMenuBar::ITEM_RPAD / 2 - sw / 2, text_y, COLOR_SHORTCUT);
      }
    }

    iy += item_h_f;
  }

  if (scrolling) {
    SDL_SetRenderClipRect(r, nullptr);

    auto draw_triangle = [&](float cx, float cy, bool point_up) {
      float hw = 5.0f, hh = 5.0f;
      SDL_FColor wh = {1.0f, 1.0f, 1.0f, 1.0f};
      SDL_Vertex verts[3];
      if (point_up) {
        verts[0] = {SDL_FPoint{cx,      cy - hh}, wh, SDL_FPoint{0, 0}};
        verts[1] = {SDL_FPoint{cx - hw, cy + hh}, wh, SDL_FPoint{0, 0}};
        verts[2] = {SDL_FPoint{cx + hw, cy + hh}, wh, SDL_FPoint{0, 0}};
      } else {
        verts[0] = {SDL_FPoint{cx - hw, cy - hh}, wh, SDL_FPoint{0, 0}};
        verts[1] = {SDL_FPoint{cx + hw, cy - hh}, wh, SDL_FPoint{0, 0}};
        verts[2] = {SDL_FPoint{cx,      cy + hh}, wh, SDL_FPoint{0, 0}};
      }
      SDL_RenderGeometry(r, nullptr, verts, 3, nullptr, 0);
    };

    if (can_up) {
      SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
      set_draw_color(r, arrow_up_hovered ? COLOR_ITEM_HL : COLOR_DROP_BG);
      fill_frect(r, panel_x + 1, panel_y + 1, (float)(panel_w - 2), (float)SCROLL_ARROW_H);
      float cx = panel_x + (float)panel_w / 2.0f;
      float cy = panel_y + (float)SCROLL_ARROW_H / 2.0f;
      draw_triangle(cx, cy, true);
    }
    if (can_down) {
      float ay = panel_y + (float)draw_h - (float)SCROLL_ARROW_H;
      SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
      set_draw_color(r, arrow_down_hovered ? COLOR_ITEM_HL : COLOR_DROP_BG);
      fill_frect(r, panel_x + 1, ay, (float)(panel_w - 2), (float)(SCROLL_ARROW_H - 1));
      float cx = panel_x + (float)panel_w / 2.0f;
      float cy = ay + (float)SCROLL_ARROW_H / 2.0f;
      draw_triangle(cx, cy, false);
    }
  }

  return draw_h;
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
    bool highlighted = (i == this->open_menu_idx) || (i == this->kbd_focused_idx);

    if (highlighted) {
      SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
      set_draw_color(r, COLOR_TITLE_HL);
      fill_frect(r, (float)tl.x, bt + 1.0f, (float)tl.width, (float)(MENUBAR_HEIGHT - 2));
    }

    int tx = tl.x + TITLE_HPAD; // tl.width == tw + 2*TITLE_HPAD, so center is always tl.x + TITLE_HPAD
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
  float drop_y = bt + (float)MENUBAR_HEIGHT;

  int natural_h = this->dropdown_height(this->open_menu_idx);
  int vis_h = this->dropdown_visible_h(drop_y, natural_h, win_h);
  int scroll_px = (int)std::max(0.0f, std::min(this->dropdown_scroll_px, (float)(natural_h - vis_h)));

  this->draw_panel(r, menu, (float)dx, drop_y, dw, this->hovered_item_idx, win_w, win_h,
      scroll_px, (vis_h < natural_h) ? vis_h : 0);

  // Draw description popup for the hovered item if it has one.
  if (this->hovered_item_idx >= 0 && this->hovered_item_idx < (int)menu->items.size()) {
    const auto& hitem = menu->items[this->hovered_item_idx];
    if (!hitem.description.empty() && hitem.enabled) {
      int item_y_off = item_y_in_dropdown(menu, this->hovered_item_idx);
      int desc_y = (int)drop_y + 2 + item_y_off - scroll_px;
      this->draw_desc_panel(r, hitem.description, dx, desc_y, dw, win_w, win_h);
    }
  }

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
        int sub_y = (int)drop_y + 2 + item_y_off - scroll_px;
        this->draw_panel(r, submenu, (float)sub_x, (float)sub_y, sub_w, this->submenu_hovered_item_idx, win_w, win_h);
      }
    }
  }
}

void SDLMenuBar::draw(SDL_Renderer* r, int win_w, int win_h, bool fullscreen) {
  if (this->force_hidden) return;
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

// ── Cursor management ─────────────────────────────────────────────────────────

void SDLMenuBar::apply_menu_cursor(bool in_menu_area) {
  if (in_menu_area) {
    if (!this->m_sword_cursor_load_attempted) {
      this->m_sword_cursor_load_attempted = true;
      try {
        this->m_sword_cursor_handle = GetCCursor(128);
      } catch (...) {
        this->m_sword_cursor_handle = nullptr;
      }
    }
    if (!this->m_sword_cursor_handle) return;
    // Lock the cursor to sword so the game's polling loop can't override it.
    LockCursorTo(this->m_sword_cursor_handle);
    this->m_cursor_overridden = true;
  } else {
    if (!this->m_cursor_overridden) return;
    UnlockCursor();
    this->m_cursor_overridden = false;
  }
}

// ── Keyboard navigation helpers ───────────────────────────────────────────────

void SDLMenuBar::close_all() {
  this->open_menu_idx = -1;
  this->hovered_item_idx = -1;
  this->submenu_open_item_idx = -1;
  this->submenu_hovered_item_idx = -1;
  if (this->submenu_timer_id) { SDL_RemoveTimer(this->submenu_timer_id); this->submenu_timer_id = 0; }
  if (this->submenu_close_timer_id) { SDL_RemoveTimer(this->submenu_close_timer_id); this->submenu_close_timer_id = 0; }
  this->submenu_pending_item_idx = -1;
  this->dropdown_scroll_px = 0.0f;
  this->scroll_up_active = false;
  this->scroll_down_active = false;
  this->kbd_focused_idx = -1;
  this->kbd_in_dropdown = false;
  this->kbd_in_submenu = false;
  this->apply_menu_cursor(false);
}

void SDLMenuBar::kbd_activate() {
  const auto& menus = this->get_top_menus();
  for (int i = 0; i < (int)menus.size(); i++) {
    if (menus[i]->enabled) {
      this->kbd_focused_idx = i;
      return;
    }
  }
}

void SDLMenuBar::kbd_open_dropdown(int /*win_h*/) {
  if (this->kbd_focused_idx < 0) return;
  const auto& menus = this->get_top_menus();
  if (this->kbd_focused_idx >= (int)menus.size()) return;
  const auto& menu = menus[this->kbd_focused_idx];
  this->open_menu_idx = this->kbd_focused_idx;
  this->kbd_focused_idx = -1;
  this->kbd_in_dropdown = true;
  this->kbd_in_submenu = false;
  this->hovered_item_idx = this->next_enabled_item(*menu, -1);
  this->submenu_open_item_idx = -1;
  this->submenu_hovered_item_idx = -1;
  this->dropdown_scroll_px = 0.0f;
}

void SDLMenuBar::kbd_scroll_to_item(int item_idx, int win_h) {
  if (item_idx < 0 || this->open_menu_idx < 0) return;
  const auto& menus = this->get_top_menus();
  if (this->open_menu_idx >= (int)menus.size()) return;
  const auto& menu = menus[this->open_menu_idx];
  if (item_idx >= (int)menu->items.size()) return;

  float drop_y = this->bar_top() + (float)MENUBAR_HEIGHT;
  int nat_h = this->dropdown_height(this->open_menu_idx);
  int vis_h = this->dropdown_visible_h(drop_y, nat_h, win_h);
  if (nat_h <= vis_h) return;

  int max_scroll = nat_h - vis_h;
  int item_y = item_y_in_dropdown(menu, item_idx);
  int ih = item_height(menu->items[item_idx]);
  int scroll = (int)this->dropdown_scroll_px;

  if (item_y < scroll + SCROLL_ARROW_H)
    scroll = std::max(0, item_y - SCROLL_ARROW_H);
  else if (item_y + ih > scroll + vis_h - SCROLL_ARROW_H)
    scroll = std::min(max_scroll, item_y + ih - vis_h + SCROLL_ARROW_H);
  this->dropdown_scroll_px = (float)scroll;
}

int SDLMenuBar::next_enabled_menu(int from) const {
  const auto& menus = this->get_top_menus();
  int n = (int)menus.size();
  for (int i = 1; i <= n; i++) {
    int idx = (from + i) % n;
    if (menus[idx]->enabled) return idx;
  }
  return from;
}

int SDLMenuBar::prev_enabled_menu(int from) const {
  const auto& menus = this->get_top_menus();
  int n = (int)menus.size();
  for (int i = 1; i <= n; i++) {
    int idx = (from - i + n) % n;
    if (menus[idx]->enabled) return idx;
  }
  return from;
}

int SDLMenuBar::next_enabled_item(const Menu& menu, int from) const {
  int n = (int)menu.items.size();
  if (n == 0) return -1;
  int start = (from < 0) ? 0 : (from + 1) % n;
  for (int i = 0; i < n; i++) {
    int idx = (start + i) % n;
    if (!is_separator(menu.items[idx]) && menu.items[idx].enabled) return idx;
  }
  return -1;
}

int SDLMenuBar::prev_enabled_item(const Menu& menu, int from) const {
  int n = (int)menu.items.size();
  if (n == 0) return -1;
  int start = (from <= 0) ? (n - 1) : (from - 1 + n) % n;
  for (int i = 0; i < n; i++) {
    int idx = (start - i + n) % n;
    if (!is_separator(menu.items[idx]) && menu.items[idx].enabled) return idx;
  }
  return -1;
}

// ── Popup menu ────────────────────────────────────────────────────────────────

int SDLMenuBar::menu_panel_width(const Menu& menu) const {
  if (!this->font) return DROPDOWN_MIN_W;
  TTF_SetFontSize(this->font, 16);
  int w = DROPDOWN_MIN_W;
  for (const auto& item : menu.items) {
    if (is_separator(item)) continue;
    int lpad = item.icon_image ? ICON_ITEM_LPAD : ITEM_LPAD;
    int iw = lpad + this->measure_text_width(item.name) + ITEM_RPAD;
    if (!is_submenu(item)) {
      std::string sc = shortcut_label(item);
      if (!sc.empty())
        iw = std::max(iw, lpad + this->measure_text_width(item.name) + 8 + this->measure_text_width(sc) + ITEM_RPAD / 2);
    }
    w = std::max(w, iw);
  }
  return w;
}

int SDLMenuBar::menu_panel_height(const Menu& menu) const {
  int h = 4;
  for (const auto& item : menu.items)
    h += is_separator(item) ? SEP_H : item_height(item);
  return h;
}

int SDLMenuBar::hit_test_popup_panel(float px, float py,
    int panel_x, int panel_y, int panel_w,
    int vis_h, int scroll_px, int natural_h,
    const Menu& menu) const {
  if (px < panel_x || px >= panel_x + panel_w) return -1;
  if (py < panel_y || py >= panel_y + vis_h) return -1;
  bool scrollable = (natural_h > vis_h);
  if (scrollable) {
    if (scroll_px > 0 && py < panel_y + SCROLL_ARROW_H) return -1;
    if (scroll_px < natural_h - vis_h && py >= panel_y + vis_h - SCROLL_ARROW_H) return -1;
  }
  float iy = (float)(panel_y + 2) - (float)scroll_px;
  for (int i = 0; i < (int)menu.items.size(); i++) {
    float ih = is_separator(menu.items[i]) ? (float)SEP_H : (float)item_height(menu.items[i]);
    if (py >= iy && py < iy + ih) {
      if (is_separator(menu.items[i]) || menu.items[i].is_header) return -1;
      return i;
    }
    iy += ih;
  }
  return -1;
}

void SDLMenuBar::draw_desc_panel(SDL_Renderer* r, const std::string& text,
    int popup_x, int popup_y, int popup_w, int win_w, int win_h) const {
  if (text.empty() || !this->font) return;
  static constexpr int DESC_MAX_W = 300;
  static constexpr int DESC_PAD = 8;
  TTF_SetFontSize(this->font, 14);
  SDL_Color white = {0xFF, 0xFF, 0xFF, 0xFF};
  SDL_Surface* surf = TTF_RenderText_Blended_Wrapped(
      this->font, text.c_str(), text.size(), white, DESC_MAX_W - 2 * DESC_PAD);
  TTF_SetFontSize(this->font, 16);
  if (!surf) return;
  SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
  int tw = surf->w, th = surf->h;
  SDL_DestroySurface(surf);
  if (!tex) return;
  SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
  int panel_w = tw + 2 * DESC_PAD;
  int panel_h = th + 2 * DESC_PAD;
  int dx = popup_x + popup_w + 4;
  if (dx + panel_w > win_w) dx = popup_x - panel_w - 4;
  if (dx < 0) dx = 0;
  int dy = popup_y;
  if (dy + panel_h > win_h) dy = win_h - panel_h;
  if (dy < 0) dy = 0;
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(r, 0x28, 0x28, 0x28, 0xF0);
  fill_frect(r, (float)dx, (float)dy, (float)panel_w, (float)panel_h);
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(r, 0x55, 0x55, 0x55, 0xFF);
  draw_frect_outline(r, (float)dx, (float)dy, (float)panel_w, (float)panel_h);
  SDL_FRect dst{(float)(dx + DESC_PAD), (float)(dy + DESC_PAD), (float)tw, (float)th};
  SDL_RenderTexture(r, tex, nullptr, &dst);
  SDL_DestroyTexture(tex);
}

int16_t SDLMenuBar::run_popup_select(std::shared_ptr<Menu> menu) {
  if (!menu || menu->items.empty() || !this->font) return 0;

  auto& wm = WindowManager::instance();
  auto sdl_win = wm.get_sdl_window();
  auto* renderer = SDL_GetRenderer(sdl_win.get());
  if (!renderer) return 0;

  int pw, ph;
  SDL_GetWindowSizeInPixels(sdl_win.get(), &pw, &ph);

  TTF_SetFontSize(this->font, 16);
  TTF_SetFontStyle(this->font, TTF_STYLE_NORMAL);

  int panel_w = this->menu_panel_width(*menu);
  int natural_h = this->menu_panel_height(*menu);

  float mx, my;
  SDL_GetMouseState(&mx, &my);
  int panel_x = std::clamp((int)mx, 0, std::max(0, pw - panel_w));
  int panel_y = (int)my;

  // If the menu doesn't fit below the cursor, slide the panel up from the cursor
  // to show as much of it as possible.
  int avail_below = ph - panel_y - 4;
  int vis_h;
  if (natural_h <= avail_below) {
    vis_h = natural_h;
  } else {
    panel_y = std::max(0, panel_y - (natural_h - avail_below));
    vis_h = std::min(natural_h, ph - panel_y - 4);
  }
  vis_h = std::max(vis_h, SCROLL_ARROW_H * 2 + ITEM_H);
  panel_y = std::clamp(panel_y, 0, std::max(0, ph - vis_h));

  int max_scroll = std::max(0, natural_h - vis_h);
  int scroll_px = 0;
  int hovered = -1;
  std::string cur_desc;
  bool done = false;
  int16_t selected = 0;

  // Arrow hover-scroll state
  bool arrow_up_hovered = false, arrow_down_hovered = false;
  uint64_t arrow_hover_start_ms = 0;
  uint64_t last_arrow_scroll_ms = 0;
  static constexpr uint64_t ARROW_INITIAL_MS = 400;
  static constexpr uint64_t ARROW_REPEAT_MS  = 80;

  while (!done) {
    wm.render_base_frame();
    TTF_SetFontSize(this->font, 16);
    TTF_SetFontStyle(this->font, TTF_STYLE_NORMAL);
    this->draw_panel(renderer, menu,
        (float)panel_x, (float)panel_y, panel_w,
        hovered, pw, ph, scroll_px, vis_h < natural_h ? vis_h : 0,
        arrow_up_hovered, arrow_down_hovered);
    if (!cur_desc.empty())
      this->draw_desc_panel(renderer, cur_desc, panel_x, panel_y, panel_w, pw, ph);
    SDL_RenderPresent(renderer);

    SDL_Event ev;
    while (SDL_PollEvent(&ev) && !done) {
      switch (ev.type) {
        case SDL_EVENT_MOUSE_MOTION: {
          int nh = this->hit_test_popup_panel(
              ev.motion.x, ev.motion.y,
              panel_x, panel_y, panel_w, vis_h, scroll_px, natural_h, *menu);
          // Don't highlight header items on hover.
          if (nh >= 0 && nh < (int)menu->items.size() && menu->items[nh].is_header)
            nh = -1;
          if (nh != hovered) {
            hovered = nh;
            cur_desc = (hovered >= 0 && hovered < (int)menu->items.size() &&
                        menu->items[hovered].enabled)
                ? menu->items[hovered].description : "";
          }
          // Update arrow hover state.
          if (max_scroll > 0) {
            float bx = ev.motion.x, by = ev.motion.y;
            bool in_panel = (bx >= panel_x && bx < panel_x + panel_w &&
                             by >= panel_y && by < panel_y + vis_h);
            bool new_up   = in_panel && scroll_px > 0 &&
                            by < panel_y + SCROLL_ARROW_H;
            bool new_down = in_panel && scroll_px < max_scroll &&
                            by >= panel_y + vis_h - SCROLL_ARROW_H;
            if (new_up != arrow_up_hovered || new_down != arrow_down_hovered) {
              arrow_up_hovered   = new_up;
              arrow_down_hovered = new_down;
              arrow_hover_start_ms  = SDL_GetTicks();
              last_arrow_scroll_ms  = 0;
            }
          } else {
            arrow_up_hovered = arrow_down_hovered = false;
          }
          break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
          if (max_scroll > 0)
            scroll_px = std::clamp(
                scroll_px - (int)(ev.wheel.y * (float)ITEM_H), 0, max_scroll);
          break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
          float bx = ev.button.x, by = ev.button.y;
          if (ev.button.button == SDL_BUTTON_LEFT) {
            int item = this->hit_test_popup_panel(
                bx, by, panel_x, panel_y, panel_w, vis_h, scroll_px, natural_h, *menu);
            bool inside_panel = (bx >= panel_x && bx < panel_x + panel_w &&
                                 by >= panel_y && by < panel_y + vis_h);
            if (item >= 0 && item < (int)menu->items.size() && menu->items[item].enabled) {
              selected = (int16_t)(item + 1);
              done = true;
            } else if (item < 0 && inside_panel && max_scroll > 0) {
              // Scroll arrow clicks (hit_test returns -1 for arrow zones too)
              if (scroll_px > 0 && by < panel_y + SCROLL_ARROW_H)
                scroll_px = std::max(0, scroll_px - ITEM_H);
              else if (scroll_px < max_scroll && by >= panel_y + vis_h - SCROLL_ARROW_H)
                scroll_px = std::min(max_scroll, scroll_px + ITEM_H);
              // else: clicked on separator or header — keep menu open, do nothing
            } else if (!inside_panel) {
              done = true;
            }
            // else: clicked a disabled/header item inside panel — keep open
          } else {
            done = true;
          }
          break;
        }
        case SDL_EVENT_KEY_DOWN: {
          SDL_Keycode k = ev.key.key;
          SDL_Keymod m = ev.key.mod;
          bool ctrl = (m & SDL_KMOD_CTRL) != 0;
          bool alt  = (m & SDL_KMOD_ALT)  != 0;
          bool gui  = (m & SDL_KMOD_GUI)  != 0;
          bool shift = (m & SDL_KMOD_SHIFT) != 0;
          if ((k == SDLK_F11 && !ctrl && !alt && !shift && !gui) ||
              (k == SDLK_F && ctrl && !alt && !gui) ||
              (k == SDLK_RETURN && alt && !ctrl && !gui) ||
              (k == SDLK_F && gui && !ctrl)) {
            WindowManager_ToggleFullscreen();
            break;
          }
          switch (k) {
            case SDLK_ESCAPE: done = true; break;
            case SDLK_UP:
              hovered = this->prev_enabled_item(*menu, hovered);
              if (hovered >= 0 && hovered < (int)menu->items.size())
                cur_desc = menu->items[hovered].description;
              break;
            case SDLK_DOWN:
              hovered = this->next_enabled_item(*menu, hovered);
              if (hovered >= 0 && hovered < (int)menu->items.size())
                cur_desc = menu->items[hovered].description;
              break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
              if (hovered >= 0 && hovered < (int)menu->items.size() &&
                  menu->items[hovered].enabled) {
                selected = (int16_t)(hovered + 1);
                done = true;
              }
              break;
            default: break;
          }
          break;
        }
        case SDL_EVENT_QUIT:
          SDL_PushEvent(&ev);
          done = true;
          break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
        case SDL_EVENT_WINDOW_MINIMIZED:
          done = true;
          break;
        default: break;
      }
    }
    // Continuous scroll while hovering over an arrow.
    if (!done && (arrow_up_hovered || arrow_down_hovered) && max_scroll > 0) {
      uint64_t now = SDL_GetTicks();
      bool first_tick  = last_arrow_scroll_ms == 0 &&
                         (now - arrow_hover_start_ms) >= ARROW_INITIAL_MS;
      bool repeat_tick = last_arrow_scroll_ms != 0 &&
                         (now - last_arrow_scroll_ms) >= ARROW_REPEAT_MS;
      if (first_tick || repeat_tick) {
        if (arrow_up_hovered)
          scroll_px = std::max(0, scroll_px - ITEM_H);
        else
          scroll_px = std::min(max_scroll, scroll_px + ITEM_H);
        last_arrow_scroll_ms = now;
        // Refresh which arrows are still valid after scroll.
        if (scroll_px == 0)        arrow_up_hovered   = false;
        if (scroll_px == max_scroll) arrow_down_hovered = false;
      }
    }

    if (!done) SDL_Delay(8);
  }

  // Clear the popup from the screen before returning — ModalDialog doesn't render.
  wm.render_base_frame();
  SDL_RenderPresent(renderer);

  return selected;
}

// ── Event handling ────────────────────────────────────────────────────────────

void SDLMenuBar::dispatch_item(int menu_idx, int item_idx) {
  auto menus = this->get_top_menus();
  if (menu_idx < 0 || menu_idx >= (int)menus.size()) return;
  const auto& menu = menus[menu_idx];
  if (item_idx < 0 || item_idx >= (int)menu->items.size()) return;
  const auto& item = menu->items[item_idx];
  if (!item.enabled || is_separator(item)) return;
  // Items flagged opens_pdf_on_click act entirely in the UI layer: the whole row
  // launches its PDF in the OS viewer and nothing is dispatched to the engine.
  if (item.opens_pdf_on_click && !item.pdf_path.empty()) {
    smb_log.info_f("Opening PDF for menu {} item {} ({})", menu->menu_id, item_idx + 1, item.name);
    open_pdf_file(item.pdf_path);
    return;
  }
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
  if (this->force_hidden) return false;
  if (!this->menu_list || !this->font) return false;

  this->rebuild_title_layouts(win_w);

  // Animation frame event: clear pending flag and drive next frame.
  if (s_anim_event_type != 0 && e.type == s_anim_event_type) {
    this->anim_event_pending = false;
    WindowManager::instance().redraw_menu_bar_only();
    return true;
  }

  // Submenu open-delay timer fired: open the pending submenu cascade.
  if (s_submenu_event_type != 0 && e.type == s_submenu_event_type) {
    if (this->submenu_pending_item_idx >= 0 && this->open_menu_idx >= 0) {
      // Cancel any in-flight close timer — the new cascade supersedes the old one.
      if (this->submenu_close_timer_id) {
        SDL_RemoveTimer(this->submenu_close_timer_id);
        this->submenu_close_timer_id = 0;
      }
      this->submenu_open_item_idx = this->submenu_pending_item_idx;
      this->submenu_pending_item_idx = -1;
      this->submenu_timer_id = 0;
      WindowManager::instance().redraw_menu_bar_only();
    }
    return true;
  }

  // Submenu close-delay timer fired: close the open submenu cascade.
  if (s_submenu_close_event_type != 0 && e.type == s_submenu_close_event_type) {
    this->submenu_close_timer_id = 0;
    if (this->submenu_open_item_idx >= 0) {
      this->submenu_open_item_idx = -1;
      this->submenu_hovered_item_idx = -1;
      WindowManager::instance().redraw_menu_bar_only();
    }
    return true;
  }

  // Snapshot visible state so we can detect changes that require a redraw.
  auto state_snapshot = [this] {
    return std::make_tuple(this->open_menu_idx, this->hovered_item_idx,
        this->submenu_open_item_idx, this->submenu_hovered_item_idx,
        this->submenu_pending_item_idx);
  };

  switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION: {
      auto before = state_snapshot();
      float px = e.motion.x;
      float py = e.motion.y;

      // Check if cursor is over a submenu panel
      if (this->open_menu_idx >= 0 && this->submenu_open_item_idx >= 0) {
        const auto& menus = this->get_top_menus();
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
                int sub_y = (int)(this->y_offset + MENUBAR_HEIGHT) + 2 + item_y_off - (int)this->dropdown_scroll_px;
                int hi = hit_test_submenu_panel(px, py, submenu, sub_x, sub_y, sub_w);
                if (hi >= 0) {
                  this->submenu_hovered_item_idx = hi;
                  // Snap parent dropdown focus to the submenu parent item.
                  this->hovered_item_idx = this->submenu_open_item_idx;
                  // Cursor committed to this panel — cancel any pending open (e.g. a
                  // different submenu item that was briefly hovered en route) and any
                  // pending close, so the current cascade stays open undisturbed.
                  if (this->submenu_timer_id) {
                    SDL_RemoveTimer(this->submenu_timer_id);
                    this->submenu_timer_id = 0;
                  }
                  this->submenu_pending_item_idx = -1;
                  if (this->submenu_close_timer_id) {
                    SDL_RemoveTimer(this->submenu_close_timer_id);
                    this->submenu_close_timer_id = 0;
                  }
                  if (state_snapshot() != before) WindowManager::instance().redraw_menu_bar_only();
                  return false;
                }
              }
            }
          }
        }
      }

      int bar_idx = this->hit_test_bar(px, py);
      if (this->open_menu_idx >= 0 && bar_idx >= 0 && bar_idx != this->open_menu_idx) {
        const auto& menus = this->get_top_menus();
        if (bar_idx < (int)menus.size() && menus[bar_idx]->enabled) {
          this->open_menu_idx = bar_idx;
          this->hovered_item_idx = -1;
          this->hovered_pdf_item_idx = -1;
          this->pdf_opening_item_idx = -1;
          this->submenu_open_item_idx = -1;
          this->submenu_hovered_item_idx = -1;
          if (this->submenu_timer_id) { SDL_RemoveTimer(this->submenu_timer_id); this->submenu_timer_id = 0; }
          if (this->submenu_close_timer_id) { SDL_RemoveTimer(this->submenu_close_timer_id); this->submenu_close_timer_id = 0; }
          this->submenu_pending_item_idx = -1;
          this->dropdown_scroll_px = 0.0f;
          this->scroll_up_active = false;
          this->scroll_down_active = false;
        }
      }
      if (this->open_menu_idx >= 0) {
        int old_pdf = this->hovered_pdf_item_idx;
        this->hovered_pdf_item_idx = this->hit_test_dropdown_pdf(px, py, win_w, win_h);
        // Any cursor movement clears the transient "opening" highlight.
        bool clear_open = (this->pdf_opening_item_idx != -1);
        this->pdf_opening_item_idx = -1;
        if (this->hovered_pdf_item_idx != old_pdf || clear_open) {
          WindowManager::instance().redraw_menu_bar_only();
        }
        int old_hovered = this->hovered_item_idx;
        this->hovered_item_idx = this->hit_test_dropdown(px, py, win_w, win_h);
        if (this->hovered_item_idx != old_hovered) {
          if (this->hovered_item_idx >= 0) {
            const auto& menus = this->get_top_menus();
            if (this->open_menu_idx < (int)menus.size()) {
              const auto& menu = menus[this->open_menu_idx];
              if (this->hovered_item_idx < (int)menu->items.size()) {
                const auto& item = menu->items[this->hovered_item_idx];
                if (is_submenu(item)) {
                  if (this->submenu_open_item_idx == this->hovered_item_idx) {
                    // Cursor returned to the item whose cascade is open — cancel close delay.
                    if (this->submenu_close_timer_id) {
                      SDL_RemoveTimer(this->submenu_close_timer_id);
                      this->submenu_close_timer_id = 0;
                    }
                  } else {
                    // Different submenu item: delay-close the old cascade and start
                    // the open delay for the new item simultaneously.
                    this->submenu_hovered_item_idx = -1;
                    if (this->submenu_open_item_idx >= 0 && !this->submenu_close_timer_id) {
                      this->submenu_close_timer_id = SDL_AddTimer(
                          SUBMENU_OPEN_DELAY_MS,
                          [](void*, SDL_TimerID, Uint32) -> Uint32 {
                            SDL_Event ev{};
                            ev.type = SDLMenuBar::s_submenu_close_event_type;
                            SDL_PushEvent(&ev);
                            return 0;
                          },
                          nullptr);
                    }
                    if (this->submenu_timer_id) {
                      SDL_RemoveTimer(this->submenu_timer_id);
                      this->submenu_timer_id = 0;
                    }
                    this->submenu_pending_item_idx = this->hovered_item_idx;
                    this->submenu_timer_id = SDL_AddTimer(
                        SUBMENU_OPEN_DELAY_MS,
                        [](void*, SDL_TimerID, Uint32) -> Uint32 {
                          SDL_Event ev{};
                          ev.type = SDLMenuBar::s_submenu_event_type;
                          SDL_PushEvent(&ev);
                          return 0;
                        },
                        nullptr);
                  }
                } else {
                  // Non-submenu item: cancel any open timer and clear pending.
                  if (this->submenu_timer_id) {
                    SDL_RemoveTimer(this->submenu_timer_id);
                    this->submenu_timer_id = 0;
                  }
                  this->submenu_pending_item_idx = -1;
                  // If a cascade is open, delay its close; otherwise close immediately.
                  if (this->submenu_open_item_idx >= 0 && !this->submenu_close_timer_id) {
                    this->submenu_close_timer_id = SDL_AddTimer(
                        SUBMENU_OPEN_DELAY_MS,
                        [](void*, SDL_TimerID, Uint32) -> Uint32 {
                          SDL_Event ev{};
                          ev.type = SDLMenuBar::s_submenu_close_event_type;
                          SDL_PushEvent(&ev);
                          return 0;
                        },
                        nullptr);
                  } else if (this->submenu_open_item_idx < 0) {
                    this->submenu_hovered_item_idx = -1;
                  }
                }
              }
            }
          } else {
            // Cursor left the dropdown: cancel open timer, delay-close any open cascade.
            if (this->submenu_timer_id) {
              SDL_RemoveTimer(this->submenu_timer_id);
              this->submenu_timer_id = 0;
            }
            this->submenu_pending_item_idx = -1;
            this->submenu_hovered_item_idx = -1;
            if (this->submenu_open_item_idx >= 0 && !this->submenu_close_timer_id) {
              this->submenu_close_timer_id = SDL_AddTimer(
                  SUBMENU_OPEN_DELAY_MS,
                  [](void*, SDL_TimerID, Uint32) -> Uint32 {
                    SDL_Event ev{};
                    ev.type = SDLMenuBar::s_submenu_close_event_type;
                    SDL_PushEvent(&ev);
                    return 0;
                  },
                  nullptr);
            } else if (this->submenu_open_item_idx < 0) {
              // Nothing open — nothing to delay.
            }
          }
        }

        // Detect cursor in scroll arrow zones.
        float drop_y = this->bar_top() + (float)MENUBAR_HEIGHT;
        int nat_h = this->dropdown_height(this->open_menu_idx);
        int vis_h = this->dropdown_visible_h(drop_y, nat_h, win_h);
        if (nat_h > vis_h) {
          float fdx = (float)this->dropdown_x(this->open_menu_idx, win_w);
          float fdw = (float)this->dropdown_width(this->open_menu_idx);
          bool in_x = (px >= fdx && px < fdx + fdw);
          int max_scroll = nat_h - vis_h;
          this->scroll_up_active = in_x && (int)this->dropdown_scroll_px > 0
              && py >= drop_y && py < drop_y + (float)SCROLL_ARROW_H;
          this->scroll_down_active = in_x && (int)this->dropdown_scroll_px < max_scroll
              && py >= drop_y + (float)vis_h - (float)SCROLL_ARROW_H && py < drop_y + (float)vis_h;
          if ((this->scroll_up_active || this->scroll_down_active)
              && !this->anim_event_pending && s_anim_event_type != 0) {
            SDL_Event aev{};
            aev.type = s_anim_event_type;
            SDL_PushEvent(&aev);
            this->anim_event_pending = true;
          }
        } else {
          this->scroll_up_active = false;
          this->scroll_down_active = false;
        }
      }
      if (state_snapshot() != before) WindowManager::instance().redraw_menu_bar_only();
      this->apply_menu_cursor((this->hit_test_bar(px, py) >= 0) || (this->open_menu_idx >= 0));
      return false;
    }

    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
      if (e.button.button != SDL_BUTTON_LEFT) return false;
      this->alt_key_pending = false;
      this->kbd_focused_idx = -1;
      this->kbd_in_dropdown = false;
      this->kbd_in_submenu = false;
      auto before = state_snapshot();
      float px = e.button.x;
      float py = e.button.y;

      // Click in submenu panel?
      if (this->open_menu_idx >= 0 && this->submenu_open_item_idx >= 0) {
        const auto& menus = this->get_top_menus();
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
                int sub_y = (int)(this->y_offset + MENUBAR_HEIGHT) + 2 + item_y_off - (int)this->dropdown_scroll_px;
                int hi = hit_test_submenu_panel(px, py, submenu, sub_x, sub_y, sub_w);
                if (hi >= 0) {
                  this->dispatch_submenu_item(this->open_menu_idx, this->submenu_open_item_idx, hi);
                  this->open_menu_idx = -1;
                  this->hovered_item_idx = -1;
                  this->submenu_open_item_idx = -1;
                  this->submenu_hovered_item_idx = -1;
                  if (this->submenu_timer_id) { SDL_RemoveTimer(this->submenu_timer_id); this->submenu_timer_id = 0; }
                  if (this->submenu_close_timer_id) { SDL_RemoveTimer(this->submenu_close_timer_id); this->submenu_close_timer_id = 0; }
                  this->submenu_pending_item_idx = -1;
                  this->dropdown_scroll_px = 0.0f;
                  this->scroll_up_active = false;
                  this->scroll_down_active = false;
                  WindowManager::instance().redraw_menu_bar_only();
                  return true;
                }
              }
            }
          }
        }
      }

      int bar_idx = this->hit_test_bar(px, py);
      if (bar_idx >= 0) {
        const auto& click_menus = this->get_top_menus();
        bool enabled = bar_idx < (int)click_menus.size() && click_menus[bar_idx]->enabled;
        if (this->open_menu_idx == bar_idx || !enabled) {
          this->open_menu_idx = -1;
          this->hovered_item_idx = -1;
        } else {
          this->open_menu_idx = bar_idx;
          this->hovered_item_idx = -1;
          this->hovered_pdf_item_idx = -1;
          this->pdf_opening_item_idx = -1;
        }
        this->submenu_open_item_idx = -1;
        this->submenu_hovered_item_idx = -1;
        if (this->submenu_timer_id) { SDL_RemoveTimer(this->submenu_timer_id); this->submenu_timer_id = 0; }
        if (this->submenu_close_timer_id) { SDL_RemoveTimer(this->submenu_close_timer_id); this->submenu_close_timer_id = 0; }
        this->submenu_pending_item_idx = -1;
        this->dropdown_scroll_px = 0.0f;
        this->scroll_up_active = false;
        this->scroll_down_active = false;
        WindowManager::instance().redraw_menu_bar_only();
        return true;
      }

      if (this->open_menu_idx >= 0) {
        // Consume clicks in scroll arrow zones without closing the menu.
        float drop_y_f = this->bar_top() + (float)MENUBAR_HEIGHT;
        int nat_h = this->dropdown_height(this->open_menu_idx);
        int vis_h = this->dropdown_visible_h(drop_y_f, nat_h, win_h);
        if (nat_h > vis_h) {
          float fdx = (float)this->dropdown_x(this->open_menu_idx, win_w);
          float fdw = (float)this->dropdown_width(this->open_menu_idx);
          if (px >= fdx && px < fdx + fdw && py >= drop_y_f && py < drop_y_f + (float)vis_h) {
            int max_scroll = nat_h - vis_h;
            if ((int)this->dropdown_scroll_px > 0 && py < drop_y_f + (float)SCROLL_ARROW_H)
              return true;
            if ((int)this->dropdown_scroll_px < max_scroll
                && py >= drop_y_f + (float)vis_h - (float)SCROLL_ARROW_H)
              return true;
          }
        }

        // A click on an item's manual-icon button opens the PDF instead of
        // launching the scenario. The button's box fills with the "opening" color
        // (presented immediately, before the blocking launch); the menu stays open
        // so the feedback persists until the cursor moves.
        int pdf_idx = this->hit_test_dropdown_pdf(px, py, win_w, win_h);
        if (pdf_idx >= 0) {
          const auto& menus = this->get_top_menus();
          if (this->open_menu_idx < (int)menus.size() &&
              pdf_idx < (int)menus[this->open_menu_idx]->items.size()) {
            this->pdf_opening_item_idx = pdf_idx;
            WindowManager::instance().redraw_menu_bar_only(); // show "opening" color now
            open_pdf_file(menus[this->open_menu_idx]->items[pdf_idx].pdf_path);
          }
          return true;
        }

        int item_idx = this->hit_test_dropdown(px, py, win_w, win_h);
        if (item_idx >= 0) {
          const auto& menus = this->get_top_menus();
          bool sub = (this->open_menu_idx < (int)menus.size() &&
                      item_idx < (int)menus[this->open_menu_idx]->items.size() &&
                      is_submenu(menus[this->open_menu_idx]->items[item_idx]));
          if (!sub) {
            this->dispatch_item(this->open_menu_idx, item_idx);
            this->open_menu_idx = -1;
            this->hovered_item_idx = -1;
            this->submenu_open_item_idx = -1;
            this->submenu_hovered_item_idx = -1;
            if (this->submenu_timer_id) { SDL_RemoveTimer(this->submenu_timer_id); this->submenu_timer_id = 0; }
            if (this->submenu_close_timer_id) { SDL_RemoveTimer(this->submenu_close_timer_id); this->submenu_close_timer_id = 0; }
            this->submenu_pending_item_idx = -1;
            this->dropdown_scroll_px = 0.0f;
            this->scroll_up_active = false;
            this->scroll_down_active = false;
            WindowManager::instance().redraw_menu_bar_only();
          }
          return true;
        }
        // Click outside: close menu
        this->open_menu_idx = -1;
        this->hovered_item_idx = -1;
        this->submenu_open_item_idx = -1;
        this->submenu_hovered_item_idx = -1;
        if (this->submenu_timer_id) { SDL_RemoveTimer(this->submenu_timer_id); this->submenu_timer_id = 0; }
        if (this->submenu_close_timer_id) { SDL_RemoveTimer(this->submenu_close_timer_id); this->submenu_close_timer_id = 0; }
        this->submenu_pending_item_idx = -1;
        this->dropdown_scroll_px = 0.0f;
        this->scroll_up_active = false;
        this->scroll_down_active = false;
        if (state_snapshot() != before) WindowManager::instance().redraw_menu_bar_only();
        return false;
      }
      return false;
    }

    case SDL_EVENT_MOUSE_WHEEL: {
      if (this->open_menu_idx < 0 || e.wheel.y == 0.0f) return false;
      float drop_y = this->bar_top() + (float)MENUBAR_HEIGHT;
      int nat_h = this->dropdown_height(this->open_menu_idx);
      int vis_h = this->dropdown_visible_h(drop_y, nat_h, win_h);
      int max_scroll = nat_h - vis_h;
      if (max_scroll <= 0) return false;
      this->dropdown_scroll_px = std::clamp(
          this->dropdown_scroll_px - e.wheel.y * (float)(ITEM_H * 3),
          0.0f, (float)max_scroll);
      WindowManager::instance().redraw_menu_bar_only();
      return true;
    }

    case SDL_EVENT_KEY_UP: {
      SDL_Keycode key = e.key.key;
      if ((key == SDLK_LALT || key == SDLK_RALT) && this->alt_key_pending) {
        this->alt_key_pending = false;
        if (this->kbd_focused_idx >= 0 || this->kbd_in_dropdown || this->open_menu_idx >= 0) {
          this->close_all();
        } else {
          this->kbd_activate();
        }
        WindowManager::instance().redraw_menu_bar_only();
        return true;
      }
      this->alt_key_pending = false;
      return false;
    }

    case SDL_EVENT_KEY_DOWN: {
      SDL_Keycode key = e.key.key;

      // Track Alt-alone press so we can toggle nav on release.
      bool is_alt = (key == SDLK_LALT || key == SDLK_RALT);
      if (is_alt) {
        if (!(e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT | SDL_KMOD_GUI)))
          this->alt_key_pending = true;
        return false;
      }
      this->alt_key_pending = false;

      // ── Escape: step back through active nav states ──────────────────────
      if (key == SDLK_ESCAPE) {
        if (this->kbd_in_submenu) {
          this->kbd_in_submenu = false;
          this->submenu_hovered_item_idx = -1;
          this->submenu_open_item_idx = -1;
          WindowManager::instance().redraw_menu_bar_only();
          return true;
        }
        if (this->kbd_in_dropdown) {
          int save = this->open_menu_idx;
          this->close_all();
          this->kbd_focused_idx = save;
          WindowManager::instance().redraw_menu_bar_only();
          return true;
        }
        if (this->kbd_focused_idx >= 0) {
          this->kbd_focused_idx = -1;
          WindowManager::instance().redraw_menu_bar_only();
          return true;
        }
        // Original: close mouse-opened submenu or dropdown
        if (this->open_menu_idx >= 0) {
          if (this->submenu_open_item_idx >= 0 || this->submenu_pending_item_idx >= 0) {
            this->submenu_open_item_idx = -1;
            this->submenu_hovered_item_idx = -1;
            if (this->submenu_timer_id) { SDL_RemoveTimer(this->submenu_timer_id); this->submenu_timer_id = 0; }
            this->submenu_pending_item_idx = -1;
          } else {
            this->open_menu_idx = -1;
            this->hovered_item_idx = -1;
            this->dropdown_scroll_px = 0.0f;
            this->scroll_up_active = false;
            this->scroll_down_active = false;
          }
          WindowManager::instance().redraw_menu_bar_only();
          return true;
        }
        return false;
      }

      // ── Bar focus mode: title highlighted, no dropdown ───────────────────
      if (this->kbd_focused_idx >= 0) {
        switch (key) {
          case SDLK_LEFT:
            this->kbd_focused_idx = this->prev_enabled_menu(this->kbd_focused_idx);
            WindowManager::instance().redraw_menu_bar_only();
            return true;
          case SDLK_RIGHT:
            this->kbd_focused_idx = this->next_enabled_menu(this->kbd_focused_idx);
            WindowManager::instance().redraw_menu_bar_only();
            return true;
          case SDLK_DOWN:
          case SDLK_RETURN:
          case SDLK_KP_ENTER:
            this->kbd_open_dropdown(win_h);
            WindowManager::instance().redraw_menu_bar_only();
            return true;
          case SDLK_UP:
            return true;
          default:
            // Any other key exits bar-focus and passes through to the game.
            this->kbd_focused_idx = -1;
            WindowManager::instance().redraw_menu_bar_only();
            return false;
        }
      }

      // ── Keyboard dropdown navigation ─────────────────────────────────────
      if (this->kbd_in_dropdown && this->open_menu_idx >= 0) {
        const auto& menus = this->get_top_menus();
        if (this->open_menu_idx < (int)menus.size()) {
          const auto& menu = menus[this->open_menu_idx];

          // Focus is inside the submenu cascade panel.
          if (this->kbd_in_submenu &&
              this->submenu_open_item_idx >= 0 &&
              this->submenu_open_item_idx < (int)menu->items.size() &&
              is_submenu(menu->items[this->submenu_open_item_idx])) {
            int16_t sub_id = (int16_t)(unsigned char)menu->items[this->submenu_open_item_idx].mark_character;
            auto sub = this->find_submenu(sub_id);
            if (sub) {
              switch (key) {
                case SDLK_UP:
                  this->submenu_hovered_item_idx = this->prev_enabled_item(*sub, this->submenu_hovered_item_idx);
                  WindowManager::instance().redraw_menu_bar_only();
                  return true;
                case SDLK_DOWN:
                  this->submenu_hovered_item_idx = this->next_enabled_item(*sub, this->submenu_hovered_item_idx);
                  WindowManager::instance().redraw_menu_bar_only();
                  return true;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_SPACE:
                  if (this->submenu_hovered_item_idx >= 0) {
                    this->dispatch_submenu_item(this->open_menu_idx, this->submenu_open_item_idx, this->submenu_hovered_item_idx);
                    this->close_all();
                    WindowManager::instance().redraw_menu_bar_only();
                  }
                  return true;
                case SDLK_LEFT:
                  this->kbd_in_submenu = false;
                  this->submenu_hovered_item_idx = -1;
                  this->submenu_open_item_idx = -1;
                  WindowManager::instance().redraw_menu_bar_only();
                  return true;
                default:
                  return true;
              }
            }
            this->kbd_in_submenu = false; // submenu not found, fall through
          }

          // Focus is in the parent dropdown.
          switch (key) {
            case SDLK_UP:
              this->hovered_item_idx = this->prev_enabled_item(*menu, this->hovered_item_idx);
              this->kbd_scroll_to_item(this->hovered_item_idx, win_h);
              WindowManager::instance().redraw_menu_bar_only();
              return true;
            case SDLK_DOWN:
              this->hovered_item_idx = this->next_enabled_item(*menu, this->hovered_item_idx);
              this->kbd_scroll_to_item(this->hovered_item_idx, win_h);
              WindowManager::instance().redraw_menu_bar_only();
              return true;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
            case SDLK_SPACE:
              if (this->hovered_item_idx >= 0 && this->hovered_item_idx < (int)menu->items.size()) {
                const auto& item = menu->items[this->hovered_item_idx];
                if (is_submenu(item)) {
                  this->submenu_open_item_idx = this->hovered_item_idx;
                  int16_t sub_id = (int16_t)(unsigned char)item.mark_character;
                  auto sub = this->find_submenu(sub_id);
                  if (sub) {
                    this->submenu_hovered_item_idx = this->next_enabled_item(*sub, -1);
                    this->kbd_in_submenu = true;
                  }
                } else {
                  this->dispatch_item(this->open_menu_idx, this->hovered_item_idx);
                  this->close_all();
                }
                WindowManager::instance().redraw_menu_bar_only();
              }
              return true;
            case SDLK_RIGHT: {
              if (this->hovered_item_idx >= 0 && this->hovered_item_idx < (int)menu->items.size() &&
                  is_submenu(menu->items[this->hovered_item_idx])) {
                const auto& item = menu->items[this->hovered_item_idx];
                this->submenu_open_item_idx = this->hovered_item_idx;
                int16_t sub_id = (int16_t)(unsigned char)item.mark_character;
                auto sub = this->find_submenu(sub_id);
                if (sub) {
                  this->submenu_hovered_item_idx = this->next_enabled_item(*sub, -1);
                  this->kbd_in_submenu = true;
                }
              } else {
                int next = this->next_enabled_menu(this->open_menu_idx);
                this->close_all();
                this->kbd_focused_idx = next;
                this->kbd_open_dropdown(win_h);
              }
              WindowManager::instance().redraw_menu_bar_only();
              return true;
            }
            case SDLK_LEFT: {
              int prev = this->prev_enabled_menu(this->open_menu_idx);
              this->close_all();
              this->kbd_focused_idx = prev;
              this->kbd_open_dropdown(win_h);
              WindowManager::instance().redraw_menu_bar_only();
              return true;
            }
            default:
              return true; // consume all keys while dropdown is keyboard-controlled
          }
        }
      }

      // ── Ctrl/Gui keyboard shortcuts ──────────────────────────────────────
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

// ── C bridge ───────────────────────────────────────────────────────────────────

// Force the menu bar hidden (1) or restore normal visibility (0). Called from
// hideMenuBar()/showMenuBar() in showlogo.c so the bar stays hidden through the
// Fantasoft logo splash.
extern "C" void SDLMenuBar_SetForceHidden(int hidden) {
  SDLMenuBar::instance().set_force_hidden(hidden != 0);
}
