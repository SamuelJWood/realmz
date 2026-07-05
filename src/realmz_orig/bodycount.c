#include "prototypes.h"
#include "variables.h"

/************************** bodycount *******************/
void bodycount(void) {
  DialogRef bodyc;
  short count = 0;
  short maxtochose = 4;
  CCrsrHandle compassnew;

  short bodyselect[maxloop], chosen = 0;

  for (t = 0; t < maxloop; t++)
    bodyselect[t] = 0;

  for (t = 0; t < nummon; t++) {
    monster[t].beenattacked = monster[t].condition[COND_RUNS_AWAY] = monster[t].condition[COND_HELPLESS] = monster[t].condition[COND_TANGLED] = monster[t].condition[COND_STUPID] = monster[t].condition[COND_CONFUSED] = monster[t].condition[COND_DEFENSE_BONUS] = 0;
    if (monster[t].cansum < 0)
      monster[t].traiter = 0;
    if ((monster[t].stamina > 0) && (!monster[t].traiter) && (monster[t].cansum))
      bodyselect[count++] = t + 1;
  }

  if (!count) {
    updatenpcmenu();
    return;
  }

  compactheap();

  bodyc = GetNewDialog(173, 0L, (WindowPtr)-1L);

  gCurrent = bodyc;
  SetPortDialogPort(bodyc);
  BackPixPat(base);
  ForeColor(yellowColor);
  MoveWindow(GetDialogWindow(bodyc), GlobalLeft + (leftshift / 2), GlobalTop + (downshift / 2), FALSE);
  ShowWindow(GetDialogWindow(bodyc));
  ErasePortRect();
  DrawDialog(bodyc);
  ForeColor(blackColor);
  BackColor(whiteColor);
  TextFont(genevafont);
  TextSize(10);

  sound(29999 + Rand(4));

  for (t = 0; t < count - 1; t++) {
    for (tt = 0; tt < count - 1; tt++) {
      if ((monster[bodyselect[tt] - 1].stamina < monster[bodyselect[tt + 1] - 1].stamina) || (monster[bodyselect[tt + 1] - 1].cansum == -1)) {
        temp = bodyselect[tt];
        bodyselect[tt] = bodyselect[tt + 1];
        bodyselect[tt + 1] = temp;
      }
    }
  }

  for (t = 0; t < 32; t++) /***** display the mandatory selection ******/
  {
    if (bodyselect[t]) {
      GetDialogItem(bodyc, t + 3, &itemType, &itemHandle, &buttonrect);
      upbutton(FALSE);
      monsterupdate(bodyselect[t] - 1, buttonrect);

      if ((monster[abs(bodyselect[t]) - 1].cansum == -1) && (chosen < 18)) {
        downbutton(FALSE);
        bodyselect[t] = -(abs(bodyselect[t]));
        chosen++;
      }
    }
  }

  for (t = 0; t < 32; t++)
    if (bodyselect[t] < 0)
      maxtochose++;
  if (maxtochose > 18)
    maxtochose = 18;

  for (t = 0; t < 10; t++) /***** add secondary selection ****/
  {
    if (bodyselect[t] > 0) {
      GetDialogItem(bodyc, t + 3, &itemType, &itemHandle, &buttonrect);

      if (chosen < maxtochose) {
        downbutton(FALSE);
        bodyselect[t] = -(abs(bodyselect[t]));
        chosen++;
      }
    }
  }

over:

  gCurrent = bodyc;
  SetPortDialogPort(bodyc);

  sound(29999 + Rand(4));

  ForeColor(blackColor);
  BackColor(whiteColor);

  BeginUpdate(GetDialogWindow(bodyc));
  EndUpdate(GetDialogWindow(bodyc));

  compassnew = NIL;

  if (((147 - chosen + maxtochose) > 146) && (147 - chosen + maxtochose < 155))
    compassnew = GetCCursor(147 - chosen + maxtochose);
  else
    compassnew = GetCCursor(147);

  if (compassnew) {
    SetCCursor(compassnew);
    DisposeCCursor(compassnew);
  }

  for (;;) {
    FlushEvents(everyEvent, 0);
    ModalDialog(0L, &itemHit);
    GetDialogItem(bodyc, itemHit, &itemType, &itemHandle, &buttonrect);

    if (itemHit == 2)
      goto out;

    if ((itemHit > 2) && (itemHit < 35)) {
      if (((chosen > (maxtochose - 1)) && (bodyselect[itemHit - 3] > 0)) || (!bodyselect[itemHit - 3])) {
      toomany:
        TextMode(1);
        TextSize(12);
        ForeColor(yellowColor);
        MyrCDiStr(41, (StringPtr) "You have already bagged your limit -OR- there is no monster here.");
        sound(6000);
        delay(80);
        MyrCDiStr(41, (StringPtr) "");
        goto over;
      } else if (bodyselect[itemHit - 3] < 0) {
        if (monster[abs(bodyselect[itemHit - 3]) - 1].cansum == -1) {
          upbutton(FALSE);
          sound(141);
          bodyselect[itemHit - 3] = abs(bodyselect[itemHit - 3]);
          chosen--;

          TextMode(1);
          TextSize(12);
          ForeColor(yellowColor);
          RGBBackColor(&greycolor);
          MyrCDiStr(41, (StringPtr) "This creature may be important to the scenario.  Leave behind at your own risk.");
          sound(6000);
          delay(80);
          MyrCDiStr(41, (StringPtr) "");
          goto over;
        }

        upbutton(FALSE);
        sound(141);
        bodyselect[itemHit - 3] = abs(bodyselect[itemHit - 3]);
        chosen--;
      } else if (chosen < maxtochose) {
        downbutton(FALSE);
        sound(141);
        bodyselect[itemHit - 3] = -(abs(bodyselect[itemHit - 3]));
        chosen++;
      } else
        goto toomany;
    }

    if (((147 - chosen + maxtochose) > 146) && (147 - chosen + maxtochose < 155))
      compassnew = GetCCursor(147 - chosen + maxtochose);
    else
      compassnew = GetCCursor(147);
    SetCCursor(compassnew);
    DisposeCCursor(compassnew);
  }

out:
  if (chosen > 18)
    chosen = 18;
  heldover = chosen;

  for (t = 0; t < 32; t++) {
    if (bodyselect[t] < 0) {
      chosen--;
      if (chosen > -1) {
        holdover[chosen] = monster[abs(bodyselect[t]) - 1];
        for (tt = 0; tt < 4; tt++)
          if (holdover[chosen].condition[tt] > 0)
            holdover[chosen].condition[tt] = 0;
        holdover[chosen].condition[COND_ABSORBING_ENERGY_FROM_ATTACKS] = 0;
#if CHECK_ILLEGAL_ACCESS > 0
        if (abs(bodyselect[t]) - 1 < 0 || abs(bodyselect[t]) - 1 >= 100)
          AcamErreur("monster bad index");
#endif
        monster[abs(bodyselect[t]) - 1].stamina = monster[abs(bodyselect[t]) - 1].hd = 0;
        for (tt = 0; tt < 6; tt++)
          monster[abs(bodyselect[t]) - 1].items[tt] = 0;
        for (tt = 0; tt < 3; tt++)
          monster[abs(bodyselect[t]) - 1].money[tt] = 0;
      }
    }
  }
  for (t = 0; t < maxmon; t++)
    monster[t].stamina = 0;
  DisposeDialog(bodyc); // Myriad
  updatenpcmenu();
}

/********************** monsterupdate *****************************/
void monsterupdate(short who, Rect where) {

  ForeColor(yellowColor);

  MoveTo(where.left + 5, where.bottom + 14);
  string(monster[who].stamina);
  MoveTo(where.left + 40, where.bottom + 14);
  string(monster[who].staminamax);

  ForeColor(whiteColor);
  MoveTo(where.left + 5, where.bottom + 29);
  string(monster[who].spellpoints);

  MoveTo(where.left + 40, where.bottom + 29);
  string(monster[who].maxspellpoints);

  if (monster[who].cansum == -1) /***** Show Allies *******/
  {
    icon.top = where.top;
    icon.left = where.left;
    icon.right = where.left + 64;
    icon.bottom = where.top + 64;
    ploticon2(2003);
  }

  iconhand = NIL;

  iconhand = GetCIcon(monster[who].iconid);

  if (!monster[who].size)
    InsetRect(&where, 19, 19);
  if (monster[who].size == 2)
    InsetRect(&where, 3, 19);
  if (monster[who].size == 1)
    InsetRect(&where, 19, 3);
  if (monster[who].size == 3)
    InsetRect(&where, 3, 3);

  if (iconhand) {
    PlotCIcon(&where, iconhand);
    DisposeCIcon(iconhand);
  }
}

/**************************** updatenpcmenu *****************/
void updatenpcmenu(void) {
  short t;
  char monstername[255];

  DisableItem(gNPC, 0);

  /* Clean out any stale ally data past the current count (Fantasoft v7.1). t is
   * a 0-based index into the size-20 holdover array here; the t < 20 bound keeps
   * it in range (the original code read one past the end). */
  for (t = 1; t < 20; t++) {
    if (t >= heldover) {
      holdover[t].name = 0;
    }
  }

  /* Rebuild the Allies dropdown so its height matches the number of allies
   * rather than always padding out to 20 blank rows. The menu carries a single
   * item at rest, so trim back down to one and then grow it as needed. */
  while (CountMItems(gNPC) > 1) {
    DeleteMenuItem(gNPC, CountMItems(gNPC));
  }

  if (heldover < 1) {
    /* No allies: leave a single disabled "None" item, like any empty menu. */
    strcpy(monstername, (StringPtr) "None");
    CtoPstr(monstername);
    SetMenuItemText(gNPC, 1, (StringPtr)monstername);
    SetItemIcon(gNPC, 1, 0);
    DisableItem(gNPC, 1);
  } else {
    EnableItem(gNPC, 0);
    for (t = 1; t <= heldover; t++) {
      if (t > CountMItems(gNPC))
        AppendMenuCStr(gNPC, "");
      EnableItem(gNPC, t);
      strcpy((StringPtr)monstername, (StringPtr)holdover[t - 1].monname);
      CtoPstr(monstername);
      SetMenuItemText(gNPC, t, (StringPtr)monstername);
      if (holdover[t - 1].iconid < 512)
        SetItemIcon(gNPC, t, holdover[t - 1].iconid);
      else
        SetItemIcon(gNPC, t, 0);
      holdover[t - 1].beenattacked = holdover[t - 1].target = 0;
    }
  }
  SetMenuBar(myMenuBar);
  InsertMenu(gSound, -1);
  InsertMenu(gSpeed, -1);
  DrawMenuBar();
}
