#include "errhandlingapi.h"
#include "windef.h"
#include "wingdi.h"
#include "winnls.h"
#include "processthreadsapi.h"
#include "winbase.h"
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

// === Description hover popup ===
static HWND g_desc_popup = NULL;
static std::shared_ptr<WinMenu> g_popup_menu;
static std::wstring g_desc_wtext;
static std::unordered_map<uint8_t, HBITMAP> icon_hbm_cache;
static std::unordered_map<int, HBITMAP> popup_icon_hbm_cache;

static HFONT make_pt_font(int pt) {
  NONCLIENTMETRICSW ncm = {};
  ncm.cbSize = sizeof(ncm);
  SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
  LOGFONTW lf = ncm.lfMenuFont;
  HDC hdc = GetDC(NULL);
  lf.lfHeight = -MulDiv(pt, GetDeviceCaps(hdc, LOGPIXELSY), 72);
  ReleaseDC(NULL, hdc);
  return CreateFontIndirectW(&lf);
}

static HFONT get_desc_font() {
  static HFONT font = nullptr;
  if (!font) font = make_pt_font(11);
  return font;
}

static HFONT get_menu_font() {
  static HFONT font = nullptr;
  if (!font) font = make_pt_font(13);
  return font;
}

static HFONT get_menu_font_bold() {
  static HFONT font = nullptr;
  if (!font) {
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    LOGFONTW lf = ncm.lfMenuFont;
    HDC hdc = GetDC(NULL);
    lf.lfHeight = -MulDiv(13, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(NULL, hdc);
    lf.lfWeight = FW_BOLD;
    font = CreateFontIndirectW(&lf);
  }
  return font;
}

static std::pair<int, int> measure_desc_text(const std::wstring& text) {
  HDC hdc = GetDC(NULL);
  HFONT old_font = (HFONT)SelectObject(hdc, get_desc_font());
  RECT rc = {0, 0, 360, 0};
  DrawTextW(hdc, text.c_str(), -1, &rc, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
  SelectObject(hdc, old_font);
  ReleaseDC(NULL, hdc);
  return {rc.right + 16, rc.bottom + 24};
}

LRESULT CALLBACK DescPopupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_PAINT) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    HFONT old_font = (HFONT)SelectObject(hdc, get_desc_font());
    RECT rc;
    GetClientRect(hwnd, &rc);
    FillRect(hdc, &rc, (HBRUSH)(COLOR_INFOBK + 1));
    SetBkColor(hdc, GetSysColor(COLOR_INFOBK));
    SetTextColor(hdc, GetSysColor(COLOR_INFOTEXT));
    InflateRect(&rc, -8, -8);
    DrawTextW(hdc, g_desc_wtext.c_str(), -1, &rc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(hdc, old_font);
    EndPaint(hwnd, &ps);
    return 0;
  }
  if (msg == WM_ERASEBKGND)
    return 1;
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ensure_desc_wndclass() {
  static bool registered = false;
  if (!registered) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DescPopupWndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hbrBackground = (HBRUSH)(COLOR_INFOBK + 1);
    wc.lpszClassName = L"RealmzDescPopup";
    RegisterClassExW(&wc);
    registered = true;
  }
}

static BOOL CALLBACK find_menu_wnd_enum(HWND hwnd, LPARAM lParam) {
  wchar_t cls[64] = {};
  GetClassNameW(hwnd, cls, 64);
  if (wcscmp(cls, L"#32768") == 0 && IsWindowVisible(hwnd)) {
    *reinterpret_cast<HWND*>(lParam) = hwnd;
    return FALSE;
  }
  return TRUE;
}

static HWND find_popup_menu_hwnd() {
  HWND result = NULL;
  EnumThreadWindows(GetCurrentThreadId(), find_menu_wnd_enum, reinterpret_cast<LPARAM>(&result));
  return result;
}

static void show_desc_popup(const std::string& utf8_text, int item_screen_top) {
  int len = MultiByteToWideChar(CP_UTF8, 0, utf8_text.c_str(), -1, nullptr, 0);
  g_desc_wtext.assign(len, 0);
  MultiByteToWideChar(CP_UTF8, 0, utf8_text.c_str(), -1, g_desc_wtext.data(), len);
  if (!g_desc_wtext.empty() && g_desc_wtext.back() == L'\0')
    g_desc_wtext.pop_back();

  auto [w, h] = measure_desc_text(g_desc_wtext);
  w = std::max(w, 200);
  h = std::max(h, 40);

  int x, y;
  HWND menu_hwnd = find_popup_menu_hwnd();
  if (menu_hwnd) {
    RECT mr;
    GetWindowRect(menu_hwnd, &mr);
    HMONITOR mon = MonitorFromRect(&mr, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfo(mon, &mi);
    RECT sf = mi.rcWork;
    // Prefer right side; fall back to left, both flush with the menu edge
    x = (mr.right + w <= sf.right) ? mr.right : (mr.left - w);
    y = (item_screen_top != 0) ? item_screen_top : mr.top;
    if (y + h > sf.bottom) y = sf.bottom - h;
    if (y < sf.top) y = sf.top;
  } else {
    POINT pt;
    GetCursorPos(&pt);
    x = pt.x + 25;
    y = pt.y - h / 2;
    int sw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int sh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    int sx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int sy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    if (x + w > sx + sw) x = pt.x - w - 25;
    if (x < sx) x = sx;
    if (y < sy) y = sy;
    if (y + h > sy + sh) y = sy + sh - h;
  }

  if (g_desc_popup) {
    SetWindowPos(g_desc_popup, HWND_TOPMOST, x, y, w, h,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    RedrawWindow(g_desc_popup, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
  }
}

static void hide_desc_popup() {
  if (g_desc_popup)
    SetWindowPos(g_desc_popup, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOMOVE | SWP_HIDEWINDOW);
}

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

  if (msg == WM_MENUSELECT) {
    WORD flags = HIWORD(wParam);
    if (flags == 0xFFFF && lParam == 0) {
      hide_desc_popup();
    } else if (g_popup_menu && !(flags & MF_POPUP) && !(flags & MF_SEPARATOR)) {
      int item_idx = static_cast<int>(LOWORD(wParam)) - 1;
      if (item_idx >= 0 && item_idx < static_cast<int>(g_popup_menu->items.size())) {
        const auto& desc = g_popup_menu->items[item_idx].description;
        if (!desc.empty()) {
          int item_screen_top = 0;
          RECT item_rect = {};
          if (GetMenuItemRect(hwnd, reinterpret_cast<HMENU>(lParam),
                              static_cast<UINT>(item_idx), &item_rect)) {
            item_screen_top = item_rect.top;
          }
          show_desc_popup(desc, item_screen_top);
        } else {
          hide_desc_popup();
        }
      }
    } else {
      hide_desc_popup();
    }
    return 0;
  }

  if (msg == WM_EXITMENULOOP) {
    hide_desc_popup();
    return CallWindowProc(g_OldWndProc, hwnd, msg, wParam, lParam);
  }

  if (msg == WM_MEASUREITEM) {
    auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
    if (mis->CtlType == ODT_MENU && g_popup_menu) {
      int item_idx = static_cast<int>(mis->itemData);
      int icon_w = 0, icon_h = 0;
      SIZE text_sz = {120, 0};
      // Menus whose first item is bold (style_flags & 1) have a portrait header
      // followed by smaller item icons (e.g. the Items/equipment popup). All
      // other menus (Race, Caste, …) show every icon at its native size.
      bool has_bold_header = !g_popup_menu->items.empty() &&
                             (g_popup_menu->items[0].style_flags & 1);
      bool is_portrait = has_bold_header && (item_idx == 0);
      bool scale_icon  = has_bold_header && (item_idx != 0);
      if (item_idx >= 0 && item_idx < static_cast<int>(g_popup_menu->items.size())) {
        const auto& mi = g_popup_menu->items[item_idx];
        if (mi.icon_image) {
          int sw = static_cast<int>(mi.icon_image->get_width());
          int sh = static_cast<int>(mi.icon_image->get_height());
          if (scale_icon) {
            float scale = std::min(32.0f / sw, 32.0f / sh);
            if (scale < 1.0f) {
              icon_w = std::max(1, static_cast<int>(sw * scale));
              icon_h = std::max(1, static_cast<int>(sh * scale));
            } else {
              icon_w = sw;
              icon_h = sh;
            }
          } else {
            icon_w = sw;
            icon_h = sh;
          }
        }
      }
      HDC hdc = GetDC(NULL);
      HFONT old_font = (HFONT)SelectObject(hdc, is_portrait ? get_menu_font_bold() : get_menu_font());
      TEXTMETRICW tm;
      GetTextMetricsW(hdc, &tm);
      if (item_idx >= 0 && item_idx < static_cast<int>(g_popup_menu->items.size())) {
        std::wstring wn(g_popup_menu->items[item_idx].name.begin(),
                        g_popup_menu->items[item_idx].name.end());
        GetTextExtentPoint32W(hdc, wn.c_str(), static_cast<int>(wn.size()), &text_sz);
      }
      SelectObject(hdc, old_font);
      ReleaseDC(NULL, hdc);
      int font_h = tm.tmHeight + tm.tmExternalLeading + 6;
      int check_col = 16;
      int icon_col = icon_w > 0 ? icon_w + 4 : 0;
      mis->itemWidth  = static_cast<UINT>(check_col + icon_col + text_sz.cx + 8);
      mis->itemHeight = static_cast<UINT>(std::max(font_h, icon_h + 4));
      return TRUE;
    }
  }

  if (msg == WM_DRAWITEM) {
    auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
    if (dis->CtlType == ODT_MENU && g_popup_menu) {
      int item_idx = static_cast<int>(dis->itemData);
      if (item_idx < 0 || item_idx >= static_cast<int>(g_popup_menu->items.size()))
        return FALSE;
      const auto& item = g_popup_menu->items[item_idx];
      bool selected = (dis->itemState & ODS_SELECTED) != 0;
      bool disabled = !item.enabled;
      bool checked = (dis->itemState & ODS_CHECKED) != 0;
      HDC hdc = dis->hDC;
      RECT rc = dis->rcItem;

      FillRect(hdc, &rc, GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_MENU));

      bool has_bold_header = !g_popup_menu->items.empty() &&
                             (g_popup_menu->items[0].style_flags & 1);
      bool is_portrait = has_bold_header && (item_idx == 0);
      bool scale_icon  = has_bold_header && (item_idx != 0);
      int icon_w = 0, icon_h = 0;
      if (item.icon_image) {
        int sw = static_cast<int>(item.icon_image->get_width());
        int sh = static_cast<int>(item.icon_image->get_height());
        if (scale_icon) {
          float scale = std::min(32.0f / sw, 32.0f / sh);
          if (scale < 1.0f) {
            icon_w = std::max(1, static_cast<int>(sw * scale));
            icon_h = std::max(1, static_cast<int>(sh * scale));
          } else {
            icon_w = sw;
            icon_h = sh;
          }
        } else {
          icon_w = sw;
          icon_h = sh;
        }
      }

      int check_col = 16;
      int icon_col = icon_w > 0 ? icon_w + 4 : 0;

      // Draw checkmark for equipped items
      if (checked) {
        RECT check_rc = {rc.left, rc.top, rc.left + check_col, rc.bottom};
        HFONT old_cf = (HFONT)SelectObject(hdc, get_menu_font());
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT));
        DrawTextW(hdc, L"✓", -1, &check_rc, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
        SelectObject(hdc, old_cf);
      }

      // Draw icon from popup-specific cache (keyed by item index)
      if (icon_w > 0) {
        auto it = popup_icon_hbm_cache.find(item_idx);
        if (it != popup_icon_hbm_cache.end() && it->second) {
          HDC mdc = CreateCompatibleDC(hdc);
          auto old_bm = (HBITMAP)SelectObject(mdc, it->second);
          int iy = rc.top + ((rc.bottom - rc.top) - icon_h) / 2;
          BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
          AlphaBlend(hdc, rc.left + check_col, iy, icon_w, icon_h, mdc, 0, 0, icon_w, icon_h, bf);
          SelectObject(mdc, old_bm);
          DeleteDC(mdc);
        }
      }

      HFONT old_font = (HFONT)SelectObject(hdc, is_portrait ? get_menu_font_bold() : get_menu_font());
      SetBkMode(hdc, TRANSPARENT);
      SetTextColor(hdc, GetSysColor(
          disabled ? COLOR_GRAYTEXT : (selected ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT)));
      std::wstring wn(item.name.begin(), item.name.end());
      RECT tr = {rc.left + check_col + icon_col, rc.top, rc.right - 4, rc.bottom};
      DrawTextW(hdc, wn.c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
      SelectObject(hdc, old_font);
      return TRUE;
    }
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

// premult_alpha=false: 24-bit DIB composited against COLOR_MENU, for hbmpItem (BitBlt).
// premult_alpha=true:  32-bit premultiplied-alpha DIB, for ownerdraw popup (AlphaBlend).
static HBITMAP make_icon_hbitmap(const phosg::ImageRGBA8888N& img, bool premult_alpha = false) {
  int w = static_cast<int>(img.get_width());
  int h = static_cast<int>(img.get_height());

  BITMAPINFOHEADER bmi = {};
  bmi.biSize        = sizeof(bmi);
  bmi.biWidth       = w;
  bmi.biHeight      = -h;
  bmi.biPlanes      = 1;
  bmi.biBitCount    = premult_alpha ? 32 : 24;
  bmi.biCompression = BI_RGB;

  void* bits = nullptr;
  HBITMAP hbm = CreateDIBSection(nullptr, reinterpret_cast<BITMAPINFO*>(&bmi),
      DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!hbm || !bits) {
    return nullptr;
  }

  // phosg RGBA8888N: 0xRRGGBBAA (R in the most-significant byte).
  const uint32_t* src = static_cast<const uint32_t*>(static_cast<const void*>(img.get_data()));

  if (premult_alpha) {
    // 32-bit BGRA, premultiplied. AlphaBlend requires premultiplied alpha.
    uint32_t* dst = static_cast<uint32_t*>(bits);
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        uint32_t p = src[y * w + x];
        uint32_t r = (p >> 24) & 0xFF;
        uint32_t g = (p >> 16) & 0xFF;
        uint32_t b = (p >>  8) & 0xFF;
        uint32_t a = (p      ) & 0xFF;
        dst[y * w + x] = (a << 24) | ((r * a / 255) << 16) | ((g * a / 255) << 8) | (b * a / 255);
      }
    }
  } else {
    // 24-bit BGR rows (DWORD-aligned), composited against the menu background color.
    // hbmpItem rendering uses BitBlt which has no alpha support.
    COLORREF bg = GetSysColor(COLOR_MENU);
    uint8_t bg_r = GetRValue(bg);
    uint8_t bg_g = GetGValue(bg);
    uint8_t bg_b = GetBValue(bg);
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
  }
  return hbm;
}

static HBITMAP make_icon_hbitmap_scaled(const phosg::ImageRGBA8888N& img, int max_w, int max_h, bool premult_alpha = false) {
  int sw = static_cast<int>(img.get_width());
  int sh = static_cast<int>(img.get_height());
  if (sw == 0 || sh == 0) return nullptr;
  float scale = std::min(static_cast<float>(max_w) / sw, static_cast<float>(max_h) / sh);
  if (scale >= 1.0f) return make_icon_hbitmap(img, premult_alpha);
  int nw = std::max(1, static_cast<int>(sw * scale));
  int nh = std::max(1, static_cast<int>(sh * scale));
  phosg::ImageRGBA8888N scaled(nw, nh);
  scaled.copy_from_with_blend(img, 0, 0, nw, nh, 0, 0, sw, sh, phosg::ResizeMode::NEAREST_NEIGHBOR);
  return make_icon_hbitmap(scaled, premult_alpha);
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

int WinCreatePopupMenu(SDL_Window* sdl_window, std::shared_ptr<WinMenu> menu, std::shared_ptr<WinMenuList> submenus) {
  auto wind_handle = get_window_handle(sdl_window);

  ensure_desc_wndclass();
  g_popup_menu = menu;
  g_desc_popup = CreateWindowExW(
      WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
      L"RealmzDescPopup", L"",
      WS_POPUP | WS_BORDER,
      0, 0, 376, 200,
      NULL, NULL, GetModuleHandleW(NULL), NULL);

  HMENU popupMenu = CreatePopupMenu();

  popup_icon_hbm_cache.clear();

  // Menus whose first item is bold have a portrait header followed by scaled
  // item icons (e.g. the Items/equipment popup). All other menus (Race, Caste,
  // …) render every icon at its native size.
  bool has_bold_header = !menu->items.empty() && (menu->items[0].style_flags & 1);

  int i = 0;
  for (const auto& item : menu->items) {
    i++;

    UINT state = (item.enabled ? MFS_ENABLED : MFS_DISABLED)
               | (item.checked ? MFS_CHECKED : MFS_UNCHECKED)
               | ((item.style_flags & 1) ? MFS_DEFAULT : 0);
    if (item.icon_image) {
      bool scale_this = has_bold_header && (i > 1);
      HBITMAP hbm = scale_this ? make_icon_hbitmap_scaled(*item.icon_image, 32, 32, true)
                               : make_icon_hbitmap(*item.icon_image, true);
      if (hbm) popup_icon_hbm_cache[i - 1] = hbm;
    }
    MENUITEMINFO item_info = MENUITEMINFO{
        .cbSize = sizeof(MENUITEMINFO),
        .fMask = MIIM_FTYPE | MIIM_ID | MIIM_STATE | MIIM_DATA,
        .fType = MFT_OWNERDRAW,
        .fState = state,
        .wID = static_cast<UINT>(i),
        .dwItemData = static_cast<ULONG_PTR>(i - 1)};
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

  for (auto& [key, hbm] : popup_icon_hbm_cache) {
    if (hbm) DeleteObject(hbm);
  }
  popup_icon_hbm_cache.clear();

  g_popup_menu = nullptr;
  if (g_desc_popup) {
    DestroyWindow(g_desc_popup);
    g_desc_popup = NULL;
  }

  return result;
}
