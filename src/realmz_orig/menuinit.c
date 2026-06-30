#include "prototypes.h"
#include "realmzbuild.h"
#include "variables.h"

/***************************** MenuInit ********************************/
void MenuInit(void) {
  short menucounter;
  short t = 0;
  FILE* fp = NULL;

  // --- Copyright menu bar (shown briefly during init) ---
  {
    MenuHandle emptyMenu = NewMenu(138, "\p");
    AppendMenuCStr(emptyMenu, "");

    copy = NewMenu(133, "\pRealmz Copyright 1994-2002, Tim Phillips.  Please pay if you play.");
    AppendMenuCStr(copy, "");
    SetItemStyle(copy, 1, 1);

    /* This menu is only ever a banner (the copyright line, and the
     * "Items"/"Shop"/"Trade"/"Treasure" labels on those screens). Disable it so
     * it reads as a label and can't be pulled down. */
    DisableItem(copy, 0);
    DisableItem(emptyMenu, 0);

    static const int16_t copywright_ids[] = {138, 133};
    copywright = Realmz_NewMBarFromMenus(copywright_ids, 2);
    SetMenuBar(copywright);
  }

  // --- Info menu (128) ---
  gApple = NewMenu(128, "\pInfo");
  AppendMenuCStr(gApple, "About Realmz\xe2\x84\xa2");
  AppendMenuCStr(gApple, "About Fantasoft");
  AppendMenuCStr(gApple, "Prelude to Pestilece");
  AppendMenuCStr(gApple, "Assault on Giant Mountain");
  AppendMenuCStr(gApple, "Destroy the Necronomicon");
  AppendMenuCStr(gApple, "Castle in the Clouds");
  AppendMenuCStr(gApple, "Griloch's Revenge");
  AppendMenuCStr(gApple, "White Dragon");
  AppendMenuCStr(gApple, "Mithril Vault");
  AppendMenuCStr(gApple, "Twin Sands of Time");
  AppendMenuCStr(gApple, "Trouble in the Sword Lands");
  AppendMenuCStr(gApple, "War in the Sword Lands");
  AppendMenuCStr(gApple, "Wrath of the Mind Lords");
  AppendMenuCStr(gApple, "Half Truth");

  // --- Game menu (129) ---
  gFile = NewMenu(129, "\pGame");
  AppendMenuCStr(gFile, "Load A Saved Game");
  SetMenuItemKey(gFile, 1, 'L');
  AppendMenuCStr(gFile, "Revert To A Previous Game");
  SetMenuItemKey(gFile, 2, 'R');
  DisableItem(gFile, 2);
  AppendMenuCStr(gFile, "Save Current Game");
  SetMenuItemKey(gFile, 3, 'S');
  DisableItem(gFile, 3);
  AppendMenuCStr(gFile, "Fast Save");
  SetMenuItemKey(gFile, 4, 'F');
  DisableItem(gFile, 4);
  AppendMenuCStr(gFile, "-");
  AppendMenuCStr(gFile, "Open Realmz Manual");
  SetMenuItemOpensPdf(gFile, 6, ":Manuals:Realmz Manual.pdf");
  AppendMenuCStr(gFile, "-");
  AppendMenuCStr(gFile, "Quit");
  SetMenuItemKey(gFile, 8, 'Q');

  // --- Adventure menu (130) ---
  gGame = NewMenu(130, "\pAdventure");
  AppendMenuCStr(gGame, "Begin New Adventure");
  SetMenuItemKey(gGame, 1, 'B');
  AppendMenuCStr(gGame, "-");
  DisableItem(gGame, 2);
  AppendMenuCStr(gGame, "End This Adventure");
  SetMenuItemKey(gGame, 3, 'E');
  DisableItem(gGame, 3);
  AppendMenuCStr(gGame, "-");

  // --- Character menu (136) ---
  gParty = NewMenu(136, "\pCharacter");
  AppendMenuCStr(gParty, "Party Order");
  SetMenuItemKey(gParty, 1, 'O');
  DisableItem(gParty, 1);
  AppendMenuCStr(gParty, "Change Character Portrait");
  SetMenuItemKey(gParty, 2, 'C');
  DisableItem(gParty, 2);
  AppendMenuCStr(gParty, "Change Character Icon");
  SetMenuItemKey(gParty, 3, 'I');
  DisableItem(gParty, 3);
  AppendMenuCStr(gParty, "-");
  AppendMenuCStr(gParty, "Generate New Character");
  SetMenuItemKey(gParty, 5, 'G');
  AppendMenuCStr(gParty, "-");
  AppendMenuCStr(gParty, "Modify Party");
  SetMenuItemKey(gParty, 7, 'M');
  DisableItem(gParty, 7);

  // --- Bestiary menu (132) ---
  gBeast = NewMenu(132, "\pBestiary");
  DisableItem(gBeast, 0);

  // --- Allies menu (146) ---
  gNPC = NewMenu(146, "\pAllies");
  for (t = 0; t < 20; t++) {
    AppendMenuCStr(gNPC, "");
    DisableItem(gNPC, t + 1);
  }

  // --- Maps/Notes menu (200) ---
  gScenario = NewMenu(200, "\pMaps/Notes");
  AppendMenuCStr(gScenario, "Note Keeper");
  SetMenuItemKey(gScenario, 1, 'N');
  AppendMenuCStr(gScenario, "Journal");
  SetMenuItemKey(gScenario, 2, 'J');
  AppendMenuCStr(gScenario, "-");

  // --- Preferences menu (137) ---
  prefer = NewMenu(137, "\pPreferences");
  AppendMenuCStr(prefer, "Toggle Fullscreen");
  SetMenuItemShortcutText(prefer, 1, "F11");
  AppendSubmenuItemCStr(prefer, "Sound Volume", 135);
  AppendSubmenuItemCStr(prefer, "Music Volume", 147);
  AppendSubmenuItemCStr(prefer, "Game Speed", 134);
  AppendMenuCStr(prefer, "Faster Spell Resolution");
  SetMenuItemKey(prefer, 5, 'H');

  // --- Music menu (145) ---
  musicmenu = NewMenu(145, "\pMusic");
  AppendMenuCStr(musicmenu, "Music Not Available In Version 8.0 or Higher (yet)");

  // --- Developer options (139) not present in any shipped version ---
  gOptions = NULL;

  // --- Sound Volume submenu (135) ---
  gSound = NewMenu(135, "\pSound Volume");
  AppendMenuCStr(gSound, ">>> Sound F/X Volume <<<");
  AppendMenuCStr(gSound, "0 (Let's hunt wabbits.)");
  AppendMenuCStr(gSound, "1");
  AppendMenuCStr(gSound, "2");
  AppendMenuCStr(gSound, "3");
  AppendMenuCStr(gSound, "4");
  AppendMenuCStr(gSound, "5");
  AppendMenuCStr(gSound, "6");
  AppendMenuCStr(gSound, "7 (Wake the neighbors!)");

  // --- Game Speed submenu (134) ---
  gSpeed = NewMenu(134, "\pGame Speed");
  AppendMenuCStr(gSpeed, "Lets Open Her Up!");
  AppendMenuCStr(gSpeed, "1");
  AppendMenuCStr(gSpeed, "2");
  AppendMenuCStr(gSpeed, "3");
  AppendMenuCStr(gSpeed, "4");
  AppendMenuCStr(gSpeed, "5");
  AppendMenuCStr(gSpeed, "6");
  AppendMenuCStr(gSpeed, "7");
  AppendMenuCStr(gSpeed, "8");
  AppendMenuCStr(gSpeed, "9");
  AppendMenuCStr(gSpeed, "ZZzzZZzzZzZz");

  // --- Music Volume submenu (147) ---
  gMusicVol = NewMenu(147, "\pMusic Volume");
  AppendMenu(gMusicVol, "\p>>> Music Volume <<<");
  AppendMenu(gMusicVol, "\p0 (Let me listen to my hard drive spin.)");
  AppendMenu(gMusicVol, "\p1");
  AppendMenu(gMusicVol, "\p2");
  AppendMenu(gMusicVol, "\p3");
  AppendMenu(gMusicVol, "\p4");
  AppendMenu(gMusicVol, "\p5");
  AppendMenu(gMusicVol, "\p6");
  AppendMenu(gMusicVol, "\p7 (Damage my eardrums, I'm an impetuous youth!)");
  SetItemIcon(gMusicVol, 2, 124); /* cicn #380: speaker off */
  SetItemIcon(gMusicVol, 9, 125); /* cicn #381: speaker on  */

  // --- Build and activate main menu bar ---
  // The Info menu (128) is intentionally omitted: per-scenario information now
  // lives in the PDF buttons on the Adventure menu, so it is no longer needed.
  // gApple/menu 128 is still created above so existing references remain valid.
  {
    static const int16_t mbar_ids[] = {129, 130, 136, 132, 146, 200, 137};
    myMenuBar = Realmz_NewMBarFromMenus(mbar_ids, 7);
    SetMenuBar(myMenuBar);
  }

  // --- Register submenus with the active menu list ---
  InsertMenu(gSound, -1);
  SetItemIcon(gSound, 2, 124); /* cicn #380: speaker off */
  SetItemIcon(gSound, 9, 125); /* cicn #381: speaker on  */
  InsertMenu(gSpeed, -1);
  InsertMenu(gMusicVol, -1);

  // --- Set icons on Maps/Notes items ---
  SetItemIcon(gScenario, 1, 126); /* cicn #382: note keeper */
  SetItemIcon(gScenario, 2, 127); /* cicn #383: journal     */

  for (t = 1; t < 22; t++) /******** fill in maps menu with blank titles *****/
  {
    MyrAppendMenu(gScenario, (Ptr) "------------");
    DisableItem(gScenario, t + 3);
  }

  if (doreg()) {
    for (menucounter = 1; menucounter < 100; menucounter++) /******** add divinity scenario names to scenario menu *****/
    {
      GetIndString(myString, 3, 1);
      GetIndString(myString, -6003 - divine, menucounter);
      if (StringWidth(myString)) {
        AppendMenu(gGame, myString);
        DisableItem(gGame, menucounter + 18);
      }
    }

    setfree(serial); /*** check for pirated code numbers ****/
  } else if (!seenit) {
    aboutrealmz();
    seenit = 1;
  }

  // Populate Adventure menu from the bundled Scenarios/ folder, plus any user
  // scenarios in Scenarios/3rd Party Scenarios/ (listed below a divider).  Runs
  // after the doreg() block so that any Divinity names appended above are stripped
  // first.  PopulateScenarioMenu returns the count of bundled Fantasoft scenarios;
  // topfantasoftsceanrio points at the divider, so anything below it (the 3rd party
  // scenarios) is treated as a 3rd-party scenario by the engine.
  {
    int fantasoft_count = PopulateScenarioMenu(gGame);
    topfantasoftsceanrio = 5 + fantasoft_count;
  }

  updatescenarioavail();

  DisableItem(gScenario, 0);
  CheckItem(prefer, 1, WindowManager_IsFullscreen());
  SetItemMark(gSpeed, oldspeed, 19);
  SetItemMark(gSound, volume + 2, 19);
  CheckItem(gMusicVol, musicvolume + 2, TRUE);

  for (t = 0; t < 20; t++)
    SetItemMark(musicmenu, t + 8, 19);

  CheckItem(musicmenu, 1, 1 - nomusic);

  if (!divine) {
    currentscenario = 5;
    if (currentscenariohold > 5)
      currentscenario = currentscenariohold;
    CheckItem(gGame, currentscenario, 1);
  } else {
    currentscenario = 2;
    if (currentscenariohold > 15)
      currentscenario = currentscenariohold;
    CheckItem(gGame, currentscenario, 1);
  }

}

/**************************** modal menu locking *************/
/* While a modal window is open (a complex encounter, the Spells dialog, or the
 * save/load dialogs) every menu-bar menu except Preferences is disabled, then
 * restored to its previous state afterward. Preferences stays enabled so the
 * player can still adjust sound/music/speed.
 *
 * These locks may nest: a complex encounter disables the menus, and then its
 * Spells option opens the Spells dialog which also calls DisableGameMenus. A
 * depth counter ensures the menus stay disabled until the outermost modal
 * releases them, and that the original enabled state is only captured/restored
 * once (by the outermost lock). */
static short gModalMenusDepth = 0;
static Boolean gSavedMenuEnabled[6];

static void modalmenus(MenuHandle menus[6]) {
  menus[0] = gFile;
  menus[1] = gGame;
  menus[2] = gParty;
  menus[3] = gBeast;
  menus[4] = gNPC;
  menus[5] = gScenario;
}

void DisableGameMenus(void) {
  MenuHandle menus[6];
  short i;
  if (gModalMenusDepth++ > 0)
    return; /* already locked by an outer modal; just track nesting depth */
  modalmenus(menus);
  for (i = 0; i < 6; i++) {
    gSavedMenuEnabled[i] = IsItemEnabled(menus[i], 0);
    DisableItem(menus[i], 0);
  }
  DrawMenuBar();
}

void EnableGameMenus(void) {
  MenuHandle menus[6];
  short i;
  if (gModalMenusDepth == 0)
    return; /* not locked */
  if (--gModalMenusDepth > 0)
    return; /* still locked by an outer modal */
  modalmenus(menus);
  for (i = 0; i < 6; i++) {
    if (gSavedMenuEnabled[i])
      EnableItem(menus[i], 0);
    else
      DisableItem(menus[i], 0);
  }
  DrawMenuBar();
}

/**************************** updatemonstermenu *************/
void updatemonstermenu(short currentload) {
  static short lastload;
  short filecount;
  short numofitems, t, tt, temp;
  char name1[255], name2[255];
  FILE* fp = NULL;
  Boolean quickload;

updatenewfile:

  filecount = count = quickload = 0;

  numofitems = CountMItems(gBeast);

  getfilename("Data MENU");

  if ((fp = MyrFopen(filename, "rb")) == NULL)
    goto neednewfile;
  {
    if (!fread(&menupos, sizeof menupos, 1, fp)) {
      fclose(fp);
      goto neednewfile;
    }
    CvtTabShortToPc(menupos, 251);
    quickload = TRUE;
    fclose(fp);
  }

neednewfile:

  if (lastload != currentload) {
    getfilename("Data MD");
    if ((fp = MyrFopen(filename, "rb")) == NULL)
      scratch(116);

    if (quickload) {
      for (t = 0; t < 250; t++) {
        if (menupos[t]) {
          count++;

          fseek(fp, (menupos[t] - 1) * sizeof monpick, SEEK_SET);
          /* !MYRIAD 12/10/99 Because fseek can be greater than the end of the file (and fread can fail)*/
          if (fread(&monpick, sizeof monpick, 1, fp) == 1)
            CvtMonsterToPc(&monpick);
          /* !MYRIAD 12/10/99 If not read, keeps the previous value of monpick */

          if ((monpick.hd) && (!monpick.notonmenu)) {
            if (monpick.hd == 255)
              goto finish;
            strcpy((StringPtr)gotword, monpick.monname);
            CtoPstr(gotword);

            if (numofitems < count)
              AppendMenu(gBeast, (StringPtr)gotword);
            else
              SetMenuItemText(gBeast, count, (StringPtr)gotword);
          }
        }
      }
    } else {
      flashmessage((StringPtr) "Loading and sorting bestiary.", 50, 70, -1, 0);

      while (fread(&monpick, sizeof monpick, 1, fp)) {
        filecount++;
        CvtMonsterToPc(&monpick);
        strcpy((StringPtr)gotword, (StringPtr) "");

        if (count > 250)
          goto finish;

        if ((monpick.hd) && (!monpick.notonmenu)) {
          if (monpick.hd == 255)
            goto finish;

          count++;
          menupos[count] = filecount;
          strcpy((StringPtr)gotword, monpick.monname);
          CtoPstr(gotword);
          if (numofitems < count)
            AppendMenu(gBeast, (StringPtr)gotword);
          else
            SetMenuItemText(gBeast, count, (StringPtr)gotword);
        }
      }
    }

  finish:

    fclose(fp);
    lastload = currentload;

    if (!quickload) {

      for (t = 1; t < count; t++) {
        for (tt = 1; tt < count; tt++) {
          GetMenuItemText(gBeast, tt, (StringPtr)name1);
          GetMenuItemText(gBeast, tt + 1, (StringPtr)name2);

          PtoCstr((StringPtr)name1);
          PtoCstr((StringPtr)name2);

          if ((strlen(name1)) && (strlen(name2))) {
            if (strcmp(name1, name2) > 0) {
              temp = menupos[tt];
              menupos[tt] = menupos[tt + 1];
              menupos[tt + 1] = temp;
              CtoPstr(name1);
              CtoPstr(name2);
              SetMenuItemText(gBeast, tt + 1, (StringPtr)name1);
              SetMenuItemText(gBeast, tt, (StringPtr)name2);
            }
          }
        }
      }
      getfilename("Data MENU");
      if ((fp = MyrFopen(filename, "w+b")) == NULL)
        scratch(117);
      CvtTabShortToPc(menupos, 251);
      fwrite(&menupos, sizeof menupos, 1, fp);
      CvtTabShortToPc(menupos, 251);
      fclose(fp);
      setfileinfo("scen", filename);
      flashmessage((StringPtr) "", 50, 70, -1, 0);
      lastload = -1;
      goto updatenewfile;
      lastload = -1;
    }
  }
}
