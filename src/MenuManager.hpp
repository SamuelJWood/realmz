#pragma once

#include <list>
#include <memory>
#include <phosg/Image.hh>
#include <resource_file/ResourceFile.hh>

struct Menu {
  struct Item {
    std::string name;
    std::string description;
    std::string shortcut_text; // freeform label shown in the shortcut column (e.g. "F11")
    std::string pdf_path;      // host path to an associated PDF; if set, a "PDF" button shows in the shortcut column
    uint8_t icon_number = 0;
    char key_equivalent = 0;
    char mark_character = 0; // In MacRoman; use decode_mac_roman if needed
    uint8_t style_flags = 0; // See TextStyleFlag
    bool enabled = true;
    bool checked = false;
    bool is_header = false; // Non-clickable, non-hoverable; inverted colors in popup
    std::shared_ptr<phosg::ImageRGBA8888N> icon_image;

    Item() = default;
    ~Item() = default;

    Item(ResourceDASM::ResourceFile::DecodedMenu::Item& item)
        : name{item.name},
          description{},
          icon_number{item.icon_number},
          key_equivalent{item.key_equivalent},
          mark_character{item.mark_character},
          style_flags{item.style_flags},
          enabled{item.enabled},
          checked{false} {}
  };

  int16_t menu_id;
  int16_t proc_id;
  std::string title;
  bool enabled;
  std::vector<Item> items;

  Menu() = default;
  ~Menu() = default;

  Menu(ResourceDASM::ResourceFile::DecodedMenu& decoded_menu)
      : menu_id{decoded_menu.menu_id},
        proc_id{decoded_menu.proc_id},
        title(decoded_menu.title),
        enabled(decoded_menu.enabled) {
    for (auto& item : decoded_menu.items) {
      // Convert DecodedMenu::Item list to Menu::Item list
      this->items.emplace_back(item);
    }
  }
};

struct MenuList {
  std::list<std::shared_ptr<Menu>> menus;
  std::list<std::shared_ptr<Menu>> submenus;
};
