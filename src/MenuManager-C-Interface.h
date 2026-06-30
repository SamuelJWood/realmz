#pragma once

#include "Types.h"

// Due to link conflicts with functions from Windows headers, we rename uses of our
// classic Mac implementations.
#define DrawMenuBar Realmz_DrawMenuBar
#define GetMenu Realmz_GetMenu
#define NewMenu Realmz_NewMenu
#define InsertMenuItem Realmz_InsertMenuItem
#define InsertSubmenuItem Realmz_InsertSubmenuItem

#ifdef __cplusplus
extern "C" {
#endif

typedef Handle MenuHandle;

Handle GetNewMBar(int16_t menuBarID);
MenuHandle GetMenuHandle(int16_t menuID);
MenuHandle GetMenu(int16_t resourceID);
void SetMenuBar(Handle menuList);
void InsertMenu(MenuHandle theMenu, int16_t beforeID);
void GetMenuItemText(MenuHandle theMenu, uint16_t item, Str255 itemString);
void DrawMenuBar();
void DeleteMenu(int16_t menuID);
void SetMenuItemText(MenuHandle theMenu, uint16_t item, ConstStr255Param itemString);
int32_t MenuSelect(Point startPt);
void DisableItem(MenuHandle theMenu, uint16_t item);
void EnableItem(MenuHandle theMenu, uint16_t item);
// Returns whether the given menu (item 0) or menu item is currently enabled.
Boolean IsItemEnabled(MenuHandle theMenu, uint16_t item);
void CheckItem(MenuHandle theMenu, uint16_t item, Boolean checked);
int32_t PopUpMenuSelect(MenuHandle menu, int16_t top, int16_t left, int16_t popUpItem);
void AppendMenu(MenuHandle menu, ConstStr255Param data);
void AppendMenuCStr(MenuHandle menu, const char* data);
int16_t CountMItems(MenuHandle theMenu);
int32_t MenuKey(int16_t ch);
MenuHandle NewMenu(int16_t menuID, ConstStr255Param menuTitle);
// Replaces the title text shown for a menu in the menu bar (e.g. the copyright
// banner menu becomes "Items"/"Shop"/"Trade"/"Treasure" on those screens).
void SetMenuTitleCStr(MenuHandle theMenu, const char* title);
void InsertMenuItem(MenuHandle theMenu, ConstStr255Param itemString, int16_t afterItem);
void InsertSubmenuItem(MenuHandle theMenu, ConstStr255Param title, int16_t subMenuID, int16_t afterItem);
void SetItemIcon(MenuHandle theMenu, int16_t item, int16_t iconIndex);
void SetItemIconByCicnId(MenuHandle theMenu, int16_t item, int16_t cicnId);
void SetItemIconFromScenarioPng(MenuHandle theMenu, int16_t item, const char* scenario_name);
// Makes the whole item open the given PDF (Mac-style path, e.g. ":Manuals:Foo.pdf")
// in the OS default viewer, with the shared manual icon shown on the left.
void SetMenuItemOpensPdf(MenuHandle theMenu, int16_t item, const char* mac_pdf_path);
// Clears items ≥5 from theMenu, scans :Scenarios: for subdirectories, and appends
// each as a disabled menu item.  Returns the number of scenarios added.
int PopulateScenarioMenu(MenuHandle theMenu);
void SetItemMark(MenuHandle theMenu, int16_t item, int16_t markChar);
// Draws a diamond in the item's left mark column: filled (permanent) or hollow
// (temporary). Used by the Conditions menu. Independent of SetItemMark/CheckItem.
void SetItemDiamond(MenuHandle theMenu, int16_t item, int16_t filled);
// Marks item as a non-clickable, non-hoverable header with inverted colors.
void SetMenuItemIsHeader(MenuHandle theMenu, int16_t item);
void GetItemMark(MenuHandle theMenu, int16_t item, int16_t* markChar);
void SetItemStyle(MenuHandle theMenu, int16_t item, int16_t style);
void SetItemDescription(MenuHandle theMenu, int16_t item, const char* description);
// Sets the freeform shortcut label shown right-aligned in the dropdown (e.g. "F11").
// Overrides the auto-generated "Ctrl+X" label from key_equivalent.
void SetMenuItemShortcutText(MenuHandle theMenu, int16_t item, const char* shortcut_text);
const char* GetDescriptionFromFile(const char* item_name);
// Returns (menu_id << 16) | item_idx_0based, or 0 if not found.
int32_t MenuManager_FindItemByKeyEquivalent(char ch);
// Creates a new menu bar handle from an array of already-registered menu IDs.
Handle Realmz_NewMBarFromMenus(const int16_t* menu_ids, int count);
// Sets the keyboard shortcut character for a menu item (1-based).
void SetMenuItemKey(MenuHandle theMenu, int16_t item, char key);
// Appends a submenu item referencing the given subMenuID.
void AppendSubmenuItemCStr(MenuHandle theMenu, const char* title, int16_t subMenuID);

#ifdef __cplusplus
}
#endif
