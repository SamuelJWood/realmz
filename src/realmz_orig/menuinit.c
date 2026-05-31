#include "prototypes.h"
#include "realmzbuild.h"
#include "variables.h"

/***************************** MenuInit ********************************/
void MenuInit(void) {
  short menucounter;
  short t = 0;
  Str255 holdversion;
  FILE* fp = NULL;

  copywright = GetNewMBar(129);
  SetMenuBar(copywright);
  copy = GetMenuHandle(133);

  strcpy(holdversion, theString);

  myMenuBar = GetNewMBar(128);
  SetMenuBar(myMenuBar);
  gScenario = GetMenuHandle(200);
  SetItemIcon(gScenario, 1, 126); /* cicn #382: note keeper */
  SetItemIcon(gScenario, 2, 127); /* cicn #383: journal     */
  gApple = GetMenuHandle(128);
  gFile = GetMenuHandle(129);
  gGame = GetMenuHandle(130);
  gOptions = GetMenuHandle(139);
  gParty = GetMenuHandle(136);
  gBeast = GetMenuHandle(132);
  prefer = GetMenuHandle(137);
  gNPC = GetMenuHandle(146);
  musicmenu = GetMenuHandle(145);

  gSound = GetMenu(135);
  InsertMenu(gSound, -1);
  SetMenuItemText(gSound, 2, "\p0 (Let's hunt wabbits.)");
  SetMenuItemText(gSound, 9, "\p7 (Wake the neighbors!)");
  SetItemIcon(gSound, 2, 124); /* cicn #380: speaker off */
  SetItemIcon(gSound, 9, 125); /* cicn #381: speaker on  */

  gSpeed = GetMenu(134);
  InsertMenu(gSpeed, -1);

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
  InsertMenu(gMusicVol, -1);

  InsertMenuItem(prefer, "\pToggle Fullscreen", 0);
  InsertSubmenuItem(prefer, "\pMusic Volume", 147, 2);

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

  // Populate Adventure menu from the bundled Scenarios/ folder.  Runs after the
  // doreg() block so that any Divinity names appended above are stripped first.
  {
    int scen_count = PopulateScenarioMenu(gGame);
    topfantasoftsceanrio = 5 + scen_count;
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
