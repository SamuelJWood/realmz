#include "prototypes.h"
#include "variables.h"

// Race descriptions are loaded at runtime from
// "Data Files/Caste and Race descriptions.txt" via GetDescriptionFromFile.


/* The "Click and hold portrait to choose a new race" prompt was removed from
 * the background art (PICT 209) so it no longer appears on the read-only in-game
 * View Race screen. Redraw it at runtime for character generation only, matching
 * the original position/style (two centered lines of yellow script text above
 * the Done button). Drawn once per DrawDialog so the letters don't accumulate
 * (thicken) as races are selected. */
static void DrawRacePrompt(void) {
  TextMode(1);
  TextFont(font);
  TextFace(bold);
  TextSize(12);
  ForeColor(yellowColor);
  strcpy((char*)myString, "Click and hold portrait");
  MoveTo(119 - TextWidth((Ptr)myString, 0, (short)strlen((char*)myString)) / 2, 14);
  MyrDrawCString((Ptr)myString);
  strcpy((char*)myString, "to choose a new race.");
  MoveTo(119 - TextWidth((Ptr)myString, 0, (short)strlen((char*)myString)) / 2, 28);
  MyrDrawCString((Ptr)myString);
}

/***************************** Race ********************************/
void Race(short mode) {
  char castecan[30][30];
  DialogRef aging, gGeneration;
  MenuHandle popup;
  short popupcons[30];
  short conditionindex;

  if (!mode) {

    openrace();

    for (tt = 0; tt < 30; tt++) {
      fread(&races, sizeof races, 1, fp);
      CvtRaceToPc(&races);
      for (t = 0; t < 30; t++) {
        castecan[tt][t] = races.cancaste[t];
      }
    }
    fclose(fp);
  } else /************ let them see them all for view info mode *******************/
  {
    for (tt = 0; tt < 30; tt++) {
      for (t = 0; t < 30; t++) {
        castecan[tt][t] = 1;
      }
    }
  }

  gGeneration = GetNewDialog(190, 0L, (WindowPtr)-1L);
  SetPortDialogPort(gGeneration);
  gCurrent = gGeneration;
  MoveWindow(GetDialogWindow(gGeneration), GlobalLeft + (leftshift / 2), GlobalTop + (downshift / 2), FALSE);
  ShowWindow(GetDialogWindow(gGeneration));
  ErasePortRect();
  DrawDialog(gGeneration);

  if (!mode) {
    characterl.race = 1;
    DrawRacePrompt();
  }

  for (t = 1; t < 30; t++) {
    loadprofile(t, 0); /**************** popup icon values ***************/
    popupcons[t - 1] = races.defaulticonset;
  }

update:

  BeginUpdate(GetDialogWindow(gGeneration));
  EndUpdate(GetDialogWindow(gGeneration));
  GetDialogItem(gGeneration, 74, &itemType, &itemHandle, &itemRect);
  plotportrait(characterl.pictid, itemRect, characterl.caste, -1);

  TextFace(bold);
  TextFont(font);
  ForeColor(yellowColor);
  BackPixPat(base);
  TextSize(20);

  loadprofile(characterl.race, 0); /**************** load race profile ***************/

  for (t = 2; t <= 13; t++)
    DialogNum(t, races.minmax[t - 2]);
  for (t = 14; t <= 19; t++)
    DialogNumNZ(t, races.attbonus[t - 14]);
  for (t = 36; t <= 43; t++)
    DialogNumNZ(t, races.plusminustohit[t - 36]);

  conditionindex = 0;

  DialogNum(44, races.basemove);
  DialogNumNZ(45, races.magres);
  DialogNumNZ(46, races.twohand);
  DialogNumNZ(47, races.missile);
  if (races.canregenerate)
    MyrCDiStr(49, (StringPtr) "Yes");
  else
    MyrCDiStr(49, (StringPtr) "No");

  switch (races.numofattacks[0]) {
    case 2:
      DialogNum(50, 1);
      DialogNum(51, 1);
      break;

    case 3:
      DialogNum(50, 3);
      DialogNum(51, 2);
      break;

    case 4:
      DialogNum(50, 2);
      DialogNum(51, 1);
      break;
  }

  DialogNum(73, races.numofattacks[1]); /**** max attacks ****/

  TextSize(16);

  for (t = 59; t <= 72; t++)
    DialogNumNZ(t, races.specialability[t - 59]);
  for (t = 28; t <= 35; t++)
    DialogNumNZ(t, races.drvbonus[t - 28]);

  GetIndString(myString, 129, characterl.race);
  MyrPascalDiStr(58, myString);

  TextSize(14);

  for (t = 0; t < 4; t++)
    MyrCDiStr(52 + t, (StringPtr) "");
  for (t = 0; t < 40; t++) {
    if (races.conditions[t]) {
      GetIndString(myString, 133, t + 1);
      MyrPascalDiStr(52 + conditionindex++, myString);
      if (conditionindex == 4)
        goto moveon;
    }
  }

moveon:

  for (;;) {
    FlushEvents(everyEvent, 0);
    ModalDialog(0L, &itemHit);

    if (itemHit == 75) /******** view aging **********/
    {
      sound(3000);
      aging = GetNewDialog(191, 0L, (WindowPtr)-1L);
      SetPortDialogPort(aging);
      gCurrent = aging;
      WindowManager_CenterWindow(GetDialogWindow(aging));
      ShowWindow(GetDialogWindow(aging));
      ErasePortRect();

      TextFace(bold);
      TextFont(font);
      ForeColor(yellowColor);
      BackPixPat(base);
      TextSize(18);

      DrawDialog(aging);
      BeginUpdate(GetDialogWindow(aging));
      EndUpdate(GetDialogWindow(aging));

      MyrCDiStr(7, (StringPtr) "Youth");
      MyrCDiStr(8, (StringPtr) "Young");
      MyrCDiStr(9, (StringPtr) "Prime");
      MyrCDiStr(10, (StringPtr) "Adult");
      MyrCDiStr(11, (StringPtr) "Senior");

      for (t = 0; t < 5; t++) {
        temp = c[charselectnew].age / 365;
        if (twixt(temp, races.agerange[t][0], races.agerange[t][1]))
          ForeColor(whiteColor);
        else
          ForeColor(yellowColor);

        DialogNum(12 + t * 17, races.agerange[t][0]);
        DialogNum(13 + t * 17, races.agerange[t][1]);

        for (tt = 0; tt < 15; tt++)
          DialogNumNZ(14 + (t * 17) + tt, races.agechange[t][tt]);
      }

      FlushEvents(everyEvent, 0);
      ModalDialog(0L, &itemHit);
      DisposeDialog(aging);
      SetPortDialogPort(gGeneration);
      gCurrent = gGeneration;
      DrawDialog(gGeneration);
      if (!mode)
        DrawRacePrompt();
      goto update;
    }

    /* Item 57 is the "Click and hold portrait to choose your race" prompt and
     * item 74 is the portrait; both open the race-selection popup. In view-only
     * mode (mode == 1, reached from the in-game character info screen) race is
     * not changeable, so ignore these clicks. Character generation (mode == 0)
     * is unaffected. */
    if ((!mode) && ((itemHit == 57) || (itemHit == 74))) {
      short item_count = 0;
      char name_cstr[256];

      popup = GetMenu(131);
      InsertMenu(popup, -1);

      for (t = 1; t <= 30; t++) {
        GetIndString(myString, 129, t);

        if (StringWidth(myString)) {
          memcpy(name_cstr, myString + 1, (unsigned char)myString[0]);
          name_cstr[(unsigned char)myString[0]] = '\0';

          item_count++;
          AppendMenu(popup, myString);
          SetItemIcon(popup, item_count, 251 + 6 * (popupcons[t - 1]));
          SetItemDescription(popup, item_count, GetDescriptionFromFile(name_cstr));
          if (!castecan[t - 1][characterl.caste - 1]) {
            DisableItem(popup, item_count);
          }
        }
      }

      GetMouse(&point);
      LocalToGlobal(&point);
      itemHit = PopUpMenuSelect(popup, point.v, point.h, 0);

      DeleteMenu(131);
      ReleaseResource((Handle)popup);

      if (itemHit) {
        characterl.race = itemHit;
        loadprofile(characterl.race, 0);
        characterl.pictid = 251 + (races.defaulticonset * 6);
        goto update;
      }
    }

    if ((itemHit == 56) || (itemHit == 1)) {
      GetDialogItem(gGeneration, 56, &itemType, &itemHandle, &buttonrect);
      ploticon3(133, buttonrect);
      sound(6001);
      goto out;
    }
  }
out:
  if (background != NIL)
    SetPortDialogPort(background);
  DisposeDialog(gGeneration);
  if (background != NIL)
    DrawDialog(background);
}
