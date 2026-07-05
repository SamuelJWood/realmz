#pragma once

#include "MenuManager.hpp"
#include "QuickDraw.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class SDLMenuBar {
public:
  static constexpr int MENUBAR_HEIGHT = 24;
  static constexpr int TRIGGER_ZONE_PX = 10;
  static constexpr float AUTO_HIDE_DELAY_S = 0.7f;
  static constexpr float ANIM_DURATION_S = 0.15f;
  // Matches Windows SPI_GETMENUSHOWDELAY default (400 ms).
  static constexpr Uint32 SUBMENU_OPEN_DELAY_MS = 400;
  static constexpr int TITLE_HPAD = 10;
  static constexpr int ITEM_VPAD = 4;
  static constexpr int ITEM_LPAD = 28;
  static constexpr int ITEM_RPAD = 60;
  static constexpr int ITEM_H = 22;
  static constexpr int ICON_SIZE = 32;
  static constexpr int ICON_ITEM_H = 36;      // ICON_SIZE + 2px padding each side
  static constexpr int ICON_ITEM_LPAD = 40;   // 4px margin + ICON_SIZE + 4px gap
  static constexpr int SEP_H = 8;
  static constexpr int DROPDOWN_MIN_W = 160;
  static constexpr int SCROLL_ARROW_H = 16;
  static constexpr float SCROLL_SPEED_PPS = 400.0f;

  static SDLMenuBar& instance();

  void init(TTF_Font* font);
  void sync(std::shared_ptr<MenuList> menu_list);

  void draw(SDL_Renderer* r, int win_w, int win_h, bool fullscreen);
  bool handle_event(const SDL_Event& e, bool fullscreen, int win_w, int win_h);
  void update(float dt, int cursor_y, bool fullscreen, int win_h);
  void on_fullscreen_changed(bool now_fullscreen);

  // Run a blocking popup menu. Returns the 1-based selected item index, or 0
  // if the user dismissed without choosing. Used by MCCreatePopupMenu on SDL platforms.
  int16_t run_popup_select(std::shared_ptr<Menu> menu);

  // Returns MENUBAR_HEIGHT in windowed mode, 0 in fullscreen (bar overlaps).
  static int reserved_top_pixels(bool fullscreen);

  // True while the user is actively in a menu: a dropdown is open (mouse) or
  // keyboard menu navigation is engaged. Used by the combat code to pause a
  // computer-controlled turn for as long as a menu is being used.
  bool is_menu_active() const {
    return this->open_menu_idx >= 0 || this->kbd_focused_idx >= 0;
  }

  // Force the menu bar fully hidden regardless of cursor position. Used during
  // the Fantasoft logo splash (hideMenuBar/showMenuBar) so the bar doesn't
  // reappear over the logo.
  void set_force_hidden(bool hidden);
  bool is_force_hidden() const { return this->force_hidden; }

private:
  SDLMenuBar() = default;
  SDLMenuBar(const SDLMenuBar&) = delete;
  SDLMenuBar& operator=(const SDLMenuBar&) = delete;

  struct TitleLayout {
    int x;
    int width;
  };

  std::shared_ptr<MenuList> menu_list;
  std::vector<std::shared_ptr<Menu>> cached_menus; // rebuilt on sync()
  TTF_Font* font = nullptr;

  int open_menu_idx = -1;
  int hovered_item_idx = -1;
  int hovered_pdf_item_idx = -1;   // dropdown item whose manual-icon button is hovered, or -1
  int pdf_opening_item_idx = -1;   // dropdown item whose PDF is being opened (shows active color), or -1
  float dropdown_scroll_px = 0.0f;
  bool scroll_up_active = false;
  bool scroll_down_active = false;

  float y_offset = 0.0f;
  float y_target = 0.0f;
  float auto_hide_timer = 0.0f;
  bool force_hidden = false;

  // Animation event: registered once, used to self-drive slide animation frames.
  static Uint32 s_anim_event_type;
  bool anim_event_pending = false;
  // Submenu open event: fired by the one-shot SDL timer after SUBMENU_OPEN_DELAY_MS.
  static Uint32 s_submenu_event_type;

  std::vector<TitleLayout> title_layouts;
  int title_layout_win_w = 0;

  // Icon texture cache: raw pointer key → SDL_Texture*
  std::unordered_map<const phosg::ImageRGBA8888N*, SDL_Texture*> icon_cache;
  // Text texture cache: "text\xFFrrggbb" → SDL_Texture*
  std::unordered_map<std::string, SDL_Texture*> text_cache;
  // Text width cache: text string → pixel width (font size 16, cleared on sync)
  std::unordered_map<std::string, int> text_width_cache;
  SDL_Renderer* cached_renderer = nullptr;
  SDL_Texture* manual_icon_tex = nullptr; // the :Data Files:manual_icon.png button image

  // Submenu state (cascade panel from a hovered submenu item)
  int submenu_open_item_idx = -1;       // item with its cascade panel currently visible
  int submenu_hovered_item_idx = -1;   // hovered item inside the cascade panel
  int submenu_pending_item_idx = -1;   // item being hovered, waiting for the open delay
  SDL_TimerID submenu_timer_id = 0;    // one-shot SDL timer for the open delay
  SDL_TimerID submenu_close_timer_id = 0; // one-shot SDL timer for the close delay
  // Dedicated event type pushed by the close timer callback.
  static Uint32 s_submenu_close_event_type;

  // Keyboard navigation state
  int kbd_focused_idx = -1;     // >= 0: bar-focus mode, title highlighted, no dropdown
  bool kbd_in_dropdown = false; // keyboard controls the currently open dropdown
  bool kbd_in_submenu = false;  // keyboard focus is inside the submenu cascade panel
  bool alt_key_pending = false; // Alt pressed alone; activate nav on release

  void rebuild_title_layouts(int win_w);
  float bar_top() const { return this->y_offset; }

  // Returns index of top-level menu under physical (px, py), or -1.
  int hit_test_bar(float px, float py) const;
  // Returns item index in open_menu_idx's dropdown, or -1.
  int hit_test_dropdown(float px, float py, int win_w, int win_h) const;
  // Returns the dropdown item index whose "PDF" button contains the point, or -1.
  int hit_test_dropdown_pdf(float px, float py, int win_w, int win_h) const;

  void draw_bar_strip(SDL_Renderer* r, int win_w);
  void draw_dropdown(SDL_Renderer* r, int win_w, int win_h);
  int draw_panel(SDL_Renderer* r, const std::shared_ptr<Menu>& menu,
      float panel_x, float panel_y, int panel_w, int highlight_item, int win_w, int win_h,
      int scroll_px = 0, int visible_h = 0,
      bool arrow_up_hovered = false, bool arrow_down_hovered = false);
  int dropdown_visible_h(float drop_y, int natural_h, int win_h) const;
  void draw_text(SDL_Renderer* r, const std::string& text, int x, int y, SDL_Color color) const;
  int measure_text_width(const std::string& text) const;
  SDL_Texture* get_icon_texture(SDL_Renderer* r, const phosg::ImageRGBA8888N* img);
  SDL_Texture* get_manual_icon_texture(SDL_Renderer* r);

  void dispatch_item(int menu_idx, int item_idx);
  void dispatch_submenu_item(int menu_idx, int submenu_item_idx, int item_idx);

  void destroy_icon_cache();
  void destroy_text_cache();

  // Returns the cached top-level menu vector (populated on sync()).
  const std::vector<std::shared_ptr<Menu>>& get_top_menus() const;
  // Returns the submenu (from menu_list->submenus) for a submenu item, or nullptr.
  std::shared_ptr<Menu> find_submenu(int16_t menu_id) const;
  int dropdown_x(int menu_idx, int win_w) const;
  int dropdown_width(int menu_idx) const;
  int dropdown_height(int menu_idx) const;
  int submenu_panel_x(int parent_dropdown_x, int parent_dropdown_w, int submenu_w, int win_w) const;
  int submenu_panel_y(int parent_dropdown_y, int submenu_item_idx_in_panel, int win_h) const;

  // Sword cursor override while hovering over the menu bar or open dropdown.
  CCrsrHandle m_sword_cursor_handle = nullptr;
  bool m_sword_cursor_load_attempted = false;
  bool m_cursor_overridden = false;
  void apply_menu_cursor(bool in_menu_area);

  // Keyboard navigation helpers
  void close_all();
  void kbd_activate();
  void kbd_open_dropdown(int win_h);
  void kbd_scroll_to_item(int item_idx, int win_h);
  int next_enabled_menu(int from) const;
  int prev_enabled_menu(int from) const;
  int next_enabled_item(const Menu& menu, int from) const;
  int prev_enabled_item(const Menu& menu, int from) const;

  // Popup menu helpers
  int menu_panel_width(const Menu& menu) const;
  int menu_panel_height(const Menu& menu) const;
  int hit_test_popup_panel(float px, float py,
      int panel_x, int panel_y, int panel_w,
      int vis_h, int scroll_px, int natural_h,
      const Menu& menu) const;
  void draw_desc_panel(SDL_Renderer* r, const std::string& text,
      int popup_x, int popup_y, int popup_w, int win_w, int win_h) const;
};
