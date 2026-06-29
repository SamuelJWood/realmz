#include "prototypes.h"
#include "variables.h"

/******************** portrait *****************************/
void portrait(short mode) {
  DialogRef portrait;
  short lastclick, racenameindex;
  GrafPtr oldport;
  short index = 0;
  short recindex = 0;
  Boolean rollover = 0;

  int initial_load = 1;
  int saved_recomposite = WindowManager_SetEnableRecomposite(0);

  compactheap();

  GetPort(&oldport);
  MyrParamText((Ptr) "Select Character Portrait", (Ptr) "", (Ptr) "", (Ptr) "");
  portrait = GetNewDialog(170, 0L, (WindowPtr)-1L);

  gCurrent = portrait;
  SetPortDialogPort(portrait);
  BackPixPat(base);
  TextFont(font);
  TextFace(bold);
  TextSize(16);
  ForeColor(yellowColor);

  needdungeonupdate = TRUE;

  {
    Rect pb;
    GetPortBounds((CGrafPtr)GetDialogWindow(portrait), &pb);
    MoveWindow(GetDialogWindow(portrait), (800 - (pb.right - pb.left)) / 2, (600 - (pb.bottom - pb.top)) / 2, FALSE);
  }
  ShowWindow(GetDialogWindow(portrait));
  ErasePortRect();
  DrawDialog(portrait);

  GetDialogItem(gCurrent, 72, &itemType, &itemHandle, &itemRect);
  pict(213, itemRect);

  GetDialogItem(gCurrent, 69, &itemType, &itemHandle, &buttonrect);
  downbutton(FALSE);
  GetDialogItem(gCurrent, 70, &itemType, &itemHandle, &buttonrect);
  downbutton(FALSE);

updaterecomended:

  loadprofile(characterl.race, 0);
  recindex = (races.defaulticonset * 6) - 6;

  // recindex = (characterl.race * 6)-6;

  if (characterl.gender == 1)
    MyrCDiStr(73, (StringPtr) "Male");
  else
    MyrCDiStr(73, (StringPtr) "Female");
  GetIndString(myString, 129, characterl.race);
  MyrPascalDiStr(74, myString);
  GetIndString(myString, 131, characterl.caste);
  MyrPascalDiStr(75, myString);

  for (t = 3; t < 9; t++) {
    GetDialogItem(portrait, t, &itemType, &itemHandle, &buttonrect);
    iconhand = GetCIcon(t + 254 + recindex);

    if (iconhand != NIL) {
      if ((**iconhand).iconBMap.bounds.bottom == 32)
        InsetRect(&buttonrect, 6, 6);
      PlotCIcon(&buttonrect, iconhand);
      if ((**iconhand).iconBMap.bounds.bottom == 32)
        InsetRect(&buttonrect, -6, -6);
      DisposeCIcon(iconhand);
    }
    upbutton(FALSE);
    if ((t + 254 + recindex) == characterl.pictid) {
      downbutton(FALSE);
      lastclick = t;
    }
  }

  gStop = FALSE;

  if (!mode) {
    for (t = 0; t <= charnum; t++) {
      GetDialogItem(portrait, 45 + t, &itemType, &itemHandle, &itemRect);
      plotportrait(c[t].pictid, itemRect, c[t].caste, -1);

      GetDialogItem(portrait, 51 + t, &itemType, &itemHandle, &itemRect);
      ploticon3(c[t].iconid, itemRect);

      if (t == charselectnew) {
        GetDialogItem(portrait, 33 + t, &itemType, &itemHandle, &itemRect);
        ploticon3(129, itemRect);
      }

      CtoPstr(c[t].name);
      ForeColor(yellowColor);
      MyrPascalDiStr(t + 63, (StringPtr)c[t].name);
      PtoCstr((StringPtr)c[t].name);
      GetDialogItem(portrait, 57 + t, &itemType, &itemHandle, &buttonrect);
      downbutton(FALSE);
    }
  } else {
    GetDialogItem(portrait, 33, &itemType, &itemHandle, &itemRect);
    pict(154, itemRect);

    GetDialogItem(portrait, 45, &itemType, &itemHandle, &itemRect);
    plotportrait(characterl.pictid, itemRect, characterl.caste, -1);

    CtoPstr(characterl.name);
    ForeColor(yellowColor);
    MyrPascalDiStr(63, (StringPtr)characterl.name);
    PtoCstr((StringPtr)characterl.name);
    GetDialogItem(portrait, 57, &itemType, &itemHandle, &buttonrect);
    downbutton(FALSE);
  }

updatelight:

  racenameindex = index / 24;
  for (tt = 76; tt < 80; tt++) {
    GetIndString(myString, 129, racenameindex * 4 + (tt - 75));
    MyrPascalDiStr(tt, myString);
  }
  if (index == 96)
    MyrCDiStr(79, (StringPtr) "Misc. Humanoid");

  for (t = 9; t <= 32; t++) {
    GetDialogItem(portrait, t, &itemType, &itemHandle, &buttonrect);
    iconhand = GetCIcon(t + 248 + index);
    EraseRect(&buttonrect);
    if (iconhand != NIL) {
      if ((**iconhand).iconBMap.bounds.bottom == 32)
        InsetRect(&buttonrect, 6, 6);
      PlotCIcon(&buttonrect, iconhand);
      if ((**iconhand).iconBMap.bounds.bottom == 32)
        InsetRect(&buttonrect, -6, -6);
      DisposeCIcon(iconhand);
    } else
      rollover = TRUE;

    upbutton(FALSE);

    if ((t + 248 + index) == characterl.pictid) {
      downbutton(FALSE);
      lastclick = t;
    }
  }

  if (initial_load) {
    initial_load = 0;
    WindowManager_RecompositeAlways();
    WindowManager_SetEnableRecomposite(saved_recomposite);
  }

  BeginUpdate(GetDialogWindow(portrait));
  EndUpdate(GetDialogWindow(portrait));

  while (gStop == FALSE) {
    FlushEvents(everyEvent, 0);
    ModalDialog(0L, &itemHit);
    GetDialogItem(portrait, itemHit, &itemType, &itemHandle, &buttonrect);

    if (itemHit < 3) {
      GetDialogItem(portrait, 2, &itemType, &itemHandle, &buttonrect);
      ploticon3(133, buttonrect);
      gStop = TRUE;
    }

    if ((itemHit > 2) && (itemHit < 9)) /**** recomende set *****/
    {
      GetDialogItem(portrait, lastclick, &itemType, &itemHandle, &buttonrect);
      upbutton(FALSE);

      GetDialogItem(portrait, itemHit, &itemType, &itemHandle, &buttonrect);
      downbutton(FALSE);

      sound(130);
      characterl.pictid = itemHit + recindex + 254;
      GetDialogItem(portrait, charselectnew + 33, &itemType, &itemHandle, &itemRect);
      pict(154, itemRect);
      GetDialogItem(portrait, charselectnew + 45, &itemType, &itemHandle, &itemRect);
      plotportrait(characterl.pictid, itemRect, characterl.caste, -1);

      lastclick = itemHit;
    }

    if ((itemHit > 8) && (itemHit < 33)) /**** alternet set *****/
    {
      GetDialogItem(portrait, lastclick, &itemType, &itemHandle, &buttonrect);
      upbutton(FALSE);

      GetDialogItem(portrait, itemHit, &itemType, &itemHandle, &buttonrect);
      downbutton(FALSE);

      sound(130);
      characterl.pictid = itemHit + index + 257 - 9;
      GetDialogItem(portrait, charselectnew + 33, &itemType, &itemHandle, &itemRect);
      pict(154, itemRect);
      GetDialogItem(portrait, charselectnew + 45, &itemType, &itemHandle, &itemRect);
      plotportrait(characterl.pictid, itemRect, characterl.caste, -1);

      lastclick = itemHit;
    }

    if (itemHit == 71) /***** change alternet set ****/
    {
      sound(130);
      if ((rollover) || (index > 95))
        index = 0;
      else
        index += 24;
      rollover = FALSE;
      goto updatelight;
    }

    if (((itemHit > 32) && (itemHit < 39)) && (!mode)) {
      sound(141);

      GetDialogItem(portrait, lastclick, &itemType, &itemHandle, &buttonrect);
      upbutton(FALSE);

      GetDialogItem(portrait, charselectnew + 33, &itemType, &itemHandle, &itemRect);
      ploticon3(130, itemRect);

      GetDialogItem(portrait, itemHit, &itemType, &itemHandle, &itemRect);
      ploticon3(129, itemRect);

      c[charselectnew] = characterl;
      charselectnew = itemHit - 33;
      characterl = c[charselectnew];

      GetDialogItem(portrait, characterl.pictid - 254, &itemType, &itemHandle, &buttonrect);
      downbutton(FALSE);

      lastclick = characterl.pictid - 254;
      goto updaterecomended;
    }
  }
  sound(141);
  c[charselectnew] = characterl;
  DisposeDialog(portrait);
  SetPort(oldport);
}

/* Background rectangle for the lock-picking instructions on the far right of
 * the main screen (top-left at 481,355). This is the area that gets erased, so
 * it stays at the original left edge to fully cover the items beneath it; the
 * text itself is drawn slightly inset (see show_picklock_instructions). The
 * height is kept short so the box ends at y=417 and doesn't overlap the buttons
 * below. */
static void picklock_instr_rect(Rect* r) {
  SetRect(r, 481, 355, 481 + 319, 355 + 62);
}

/* Print the lock-picking instructions in yellow in the far-right rectangle. */
static void show_picklock_instructions(void) {
  GrafPtr oldPort;
  TEHandle te;
  Rect r, textrect;
  const char* msg =
      "Pick Lock mini-game:\n"
      "Click the moving bars to start or stop them.\n"
      "If all are green, you are successful.";

  if (screen == NIL)
    return;

  GetPort(&oldPort);
  SetPort(GetWindowPort(screen));
  picklock_instr_rect(&r);
  BackPixPat(base);
  ForeColor(yellowColor);
  TextFont(defaultfont);
  TextSize(12);
  TextFace(0);
  TextMode(1);
  EraseRect(&r);
  /* Draw the text 3px to the right of the (unshifted) background rect, so the
   * erased area still covers the items beneath it. */
  textrect = r;
  textrect.left += 3;
  textrect.right += 3;
  te = TENew(&textrect, &textrect);
  TESetText((Ptr)msg, (int32_t)strlen(msg), te);
  TEUpdate(&textrect, te);
  TEDispose(te);
  SetPort(oldPort);
}

/* Wipe the instructions once the mini-game is done. */
static void erase_picklock_instructions(void) {
  GrafPtr oldPort;
  Rect r;

  if (screen == NIL)
    return;

  GetPort(&oldPort);
  SetPort(GetWindowPort(screen));
  picklock_instr_rect(&r);
  BackPixPat(base);
  EraseRect(&r);
  SetPort(oldPort);
}

/*********************** picklock ****************/
short picklock(short who, short type) {
  GrafPtr oldPort;
  Rect r1, temprect;
  short one[6], delta, jump, chance;
  short yellow, green, tumblers, temp, temp2;
  int32_t start, limit;
  Boolean stopped[6], allstopped, done, fail;

  if ((who < 0) || (who > charnum))
    return (0);

  tumblers = thief.tumblers;
  if (tumblers > 6)
    tumblers = 6;

  chance = c[who].spec[5 + type] + thief.modifer[type];

  jump = 20;

  if (chance > 90)
    chance = 90;

  if (jump < 5)
    jump = 5;

  GetPort(&oldPort);
  yellow = 200 - 2 * chance;
  /* Halve the yellow zone and give that width to green, making it easier to
     stop a bar in the green. (Yellow width was green-yellow == chance.) */
  green = yellow + chance / 2;

  for (t = 0; t < 6; t++)
    one[t] = 8 + yellow / 2;

  /* Explain the mini-game on the right of the screen, then hold on a
     "Click to begin." prompt so the moving-bars dialog only appears once the
     player is ready. */
  show_picklock_instructions();
  flashmessage((StringPtr) "Click to begin.", 30, 100, 0, 6001);

  gGeneration = GetNewDialog(142, 0L, (WindowPtr)-1L);
  SetPortDialogPort(gGeneration);
  BackPixPat(base);
  gCurrent = gGeneration;
  ErasePortRect();
  ForeColor(yellowColor);
  SizeWindow(GetDialogWindow(gGeneration), 215, 35 + 35 * tumblers, FALSE);
  MoveWindow(GetDialogWindow(gGeneration), (800 - 215) / 2, (600 - (35 + 35 * tumblers)) / 2, FALSE);
  ShowWindow(GetDialogWindow(gGeneration));
  DrawDialog(gGeneration);
  GetDialogItem(gGeneration, 1, &itemType, &itemHandle, &temprect);

  while (Button()) {
  }

  FlushEvents(everyEvent, 0);
  start = TickCount();
  limit = (start + tumblers * 120) + 180;

  for (t = 0; t < 6; t++)
    stopped[t] = FALSE;

  fail = FALSE;
  done = FALSE;

  while (!done) {
    delay(2);
    temp = ((limit - TickCount()) / 60);
    ForeColor(yellowColor);
    if (temp != temp2) {
      DialogNum(15, temp);
      sound(10129);
    }

    temp2 = temp;
    if (!temp) {
      /* Time ran out before every bar was stopped in the green zone. */
      fail = TRUE;
      goto out;
    }

    /* A mouse click toggles the bar under the cursor between stopped and
       moving, so each tumbler is stopped (and can be resumed) on its own. */
    if (Button()) {
      GetMouse(&point);
      for (t = 0; t < tumblers; t++) {
        GetDialogItem(gGeneration, 1 + t, &itemType, &itemHandle, &r1);
        if (PtInRect(point, &r1)) {
          stopped[t] = !stopped[t];
          sound(130);
          if (stopped[t]) {
            /* Outline a frozen bar with a 2-pixel black border so the
               player can see at a glance which tumblers are being held. */
            ForeColor(blackColor);
            PenSize(2, 2);
            FrameRect(&r1);
            PenSize(1, 1);
          } else {
            /* Resuming: wipe the border (and stale fill); the move loop
               repaints the bar on its next pass. */
            EraseRect(&r1);
          }
          break;
        }
      }
      while (Button()) {
      }
    }

    allstopped = TRUE;
    for (t = 0; t < tumblers; t++) {
      /* Stopped bars stay frozen; moving bars never lock at their maximum,
         they keep oscillating until the player stops them. */
      if (stopped[t])
        continue;
      allstopped = FALSE;

      GetDialogItem(gGeneration, 1 + t, &itemType, &itemHandle, &r1);
      temprect = r1;
      RGBForeColor(&greycolor);
      delta = Rand(jump);
      /* Unbiased walk that bounces off both ends, so the bar sweeps the
         whole range and the size of the green zone (which grows with the
         chance of success) sets how easy it is to stop a bar in green. */
      if (Rand(100) <= 50) {
        one[t] += delta;
        if (one[t] > 208)
          one[t] = 208;
      } else {
        one[t] -= delta;
        if (one[t] < 10)
          one[t] = 10;
        temprect.left = one[t];
        EraseRect(&temprect);
      }

      ForeColor(redColor);
      r1.right = 8 + one[t];
      if (r1.right > yellow)
        r1.right = yellow;
      PaintRect(&r1);

      if (one[t] > yellow) {
        ForeColor(yellowColor);
        r1.right = 8 + one[t];
        if (r1.right > green)
          r1.right = green;
        r1.left = yellow;
        PaintRect(&r1);
      }

      if (one[t] >= green) {
        ForeColor(greenColor);
        r1.left = green;
        r1.right = 8 + one[t];
        if (r1.right > 208)
          r1.right = 208;
        PaintRect(&r1);
      }
    }

    /* Success only once every bar is stopped and sitting in the green zone
       (yellow is no longer good enough). */
    if (allstopped) {
      done = TRUE;
      for (t = 0; t < tumblers; t++)
        if (one[t] < green)
          done = FALSE;
    }
  }
out:
  delay(50);
  /* The loop only finishes normally when every bar is stopped in green, so
     success is simply "we did not run out of time". */
  reply = !fail;
  DisposeDialog(gGeneration);
  erase_picklock_instructions();
  SetPort(oldPort);
  return (reply);
}
