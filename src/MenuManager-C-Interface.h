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
void CheckItem(MenuHandle theMenu, uint16_t item, Boolean checked);
int32_t PopUpMenuSelect(MenuHandle menu, int16_t top, int16_t left, int16_t popUpItem);
void AppendMenu(MenuHandle menu, ConstStr255Param data);
int16_t CountMItems(MenuHandle theMenu);
int32_t MenuKey(int16_t ch);
MenuHandle NewMenu(int16_t menuID, ConstStr255Param menuTitle);
void InsertMenuItem(MenuHandle theMenu, ConstStr255Param itemString, int16_t afterItem);
void InsertSubmenuItem(MenuHandle theMenu, ConstStr255Param title, int16_t subMenuID, int16_t afterItem);
void SetItemIcon(MenuHandle theMenu, int16_t item, int16_t iconIndex);
void SetItemMark(MenuHandle theMenu, int16_t item, int16_t markChar);
void GetItemMark(MenuHandle theMenu, int16_t item, int16_t* markChar);
void SetItemStyle(MenuHandle theMenu, int16_t item, int16_t style);

#ifdef __cplusplus
}
#endif
