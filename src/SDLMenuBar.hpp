#pragma once

#include "MenuManager.hpp"

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
  static constexpr float AUTO_HIDE_DELAY_S = 1.5f;
  static constexpr float ANIM_DURATION_S = 0.15f;
  static constexpr int TITLE_HPAD = 10;
  static constexpr int ITEM_VPAD = 4;
  static constexpr int ITEM_LPAD = 28;
  static constexpr int ITEM_RPAD = 60;
  static constexpr int ITEM_H = 22;
  static constexpr int SEP_H = 8;
  static constexpr int DROPDOWN_MIN_W = 160;

  static SDLMenuBar& instance();

  void init(TTF_Font* font);
  void sync(std::shared_ptr<MenuList> menu_list);

  void draw(SDL_Renderer* r, int win_w, int win_h, bool fullscreen);
  bool handle_event(const SDL_Event& e, bool fullscreen, int win_w, int win_h);
  void update(float dt, int cursor_y, bool fullscreen);
  void on_fullscreen_changed(bool now_fullscreen);

  // Returns MENUBAR_HEIGHT in windowed mode, 0 in fullscreen (bar overlaps).
  static int reserved_top_pixels(bool fullscreen);

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

  float y_offset = 0.0f;
  float y_target = 0.0f;
  float auto_hide_timer = 0.0f;

  std::vector<TitleLayout> title_layouts;
  int title_layout_win_w = 0;

  // Icon texture cache: raw pointer key → SDL_Texture*
  std::unordered_map<const phosg::ImageRGBA8888N*, SDL_Texture*> icon_cache;
  // Text texture cache: "text\xFFrrggbb" → SDL_Texture*
  std::unordered_map<std::string, SDL_Texture*> text_cache;
  // Text width cache: text string → pixel width (font size 16, cleared on sync)
  std::unordered_map<std::string, int> text_width_cache;
  SDL_Renderer* cached_renderer = nullptr;

  // Submenu state (cascade panel from a hovered submenu item)
  int submenu_open_item_idx = -1;   // item index in open_menu_idx's dropdown that has a submenu open
  int submenu_hovered_item_idx = -1; // hovered item index inside that submenu panel

  void rebuild_title_layouts(int win_w);
  float bar_top() const { return this->y_offset; }

  // Returns index of top-level menu under physical (px, py), or -1.
  int hit_test_bar(float px, float py) const;
  // Returns item index in open_menu_idx's dropdown, or -1.
  int hit_test_dropdown(float px, float py, int win_w, int win_h) const;

  void draw_bar_strip(SDL_Renderer* r, int win_w);
  void draw_dropdown(SDL_Renderer* r, int win_w, int win_h);
  int draw_panel(SDL_Renderer* r, const std::shared_ptr<Menu>& menu,
      float panel_x, float panel_y, int panel_w, int highlight_item, int win_w, int win_h);
  void draw_text(SDL_Renderer* r, const std::string& text, int x, int y, SDL_Color color) const;
  int measure_text_width(const std::string& text) const;
  SDL_Texture* get_icon_texture(SDL_Renderer* r, const phosg::ImageRGBA8888N* img);

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
};
