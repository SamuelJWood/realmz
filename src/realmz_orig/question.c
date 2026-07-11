#include "prototypes.h"
#include "variables.h"

/***************************** question ********************************/
short question(short stringnum) {
  GrafPtr oldport;
  Boolean doin = TRUE;
  DialogRef question, dummy;
  Boolean wascombat = incombat;
  GetPort(&oldport);
  SetCCursor(ask);

  for (t = 0; t <= numchannel; t++)
    quiet(t);
  compactheap();

  sound(20005);

  if (stringnum < 0)
    doin = FALSE;

  if (doin)
    in();

  stringnum = abs(stringnum);

  question = GetNewDialog(137, 0L, (WindowPtr)-1L);
  SetPortDialogPort(question);
  BackPixPat(base);
  TextFont(defaultfont);
  ForeColor(yellowColor);

  GetIndString(myString, 135, stringnum);
  ParamText(myString, (StringPtr) "", (StringPtr) "", (StringPtr) "");

  /* Position depends on which prompt this is (index into STR# 135):
   *   #3  "Do you wish to erase this character forever?" and
   *   #13 "...party is dangerously under-leveled..." — both shown on the
   *       party-setup screen; place them together over the character list.
   *   #2  "Do you wish to keep this character?" and
   *   #17 "You still have spell selection points. Finish anyway?" — keep their
   *       existing (legacy) fixed position.
   *   everything else — "Do you wish to save the game before quitting?" (#1) and
   *       the various in-game prompts — center over the main gameplay map area
   *       (lookrect). */
  {
    short qx, qy;
    if (stringnum == 3 || stringnum == 13) {
      qx = GlobalLeft + 262 + leftshift; /* erase legacy 384 - 99 - 23 */
      qy = GlobalTop - 27 + downshift;   /* erase legacy 182 - 125 - 84 */
    } else if (stringnum == 2 || stringnum == 17) {
      qx = GlobalLeft + 384 + leftshift;
      qy = GlobalTop + 182 + downshift;
    } else {
      /* The map (lookrect) spans (0,0)-(320+leftshift, 320+downshift) inside the
       * game window, which sits at (GlobalLeft+1+leftshift/2, GlobalTop+1+downshift/2),
       * so its center in global coords is (GlobalLeft+161+leftshift,
       * GlobalTop+161+downshift). Nudged 80 left / 49 up (161->81, 161->112) and
       * offset by half the dialog's size to center it. */
      Rect pb;
      GetPortBounds((CGrafPtr)GetDialogWindow(question), &pb);
      qx = GlobalLeft + 81 + leftshift - (pb.right - pb.left) / 2;
      qy = GlobalTop + 112 + downshift - (pb.bottom - pb.top) / 2;
    }
    MoveWindow(GetDialogWindow(question), qx, qy, FALSE);
  }
  ShowWindow(GetDialogWindow(question));
  ErasePortRect();

over:

  FlushEvents(everyEvent, 0);

  for (;;) {
    WaitNextEvent(everyEvent, &gTheEvent, 0L, 0L);
#ifdef PC // Myriad
    DoCorrectBugMADRepeat();
#endif

    /* Keep the whole menu bar working while this prompt blocks the main loop
     * (fullscreen, load/revert, end adventure, view allies/bestiary, ...). Menu
     * interactions arrive as a synthetic mouseDown with both coordinates negative.
     * If the choice tears the game down (revert/load, or the adventure ended),
     * answer "no" so the caller unwinds. */
    if (gTheEvent.what == mouseDown && gTheEvent.where.v < 0 && gTheEvent.where.h < 0) {
      short menu_id = -gTheEvent.where.v;
      short menu_itm = -gTheEvent.where.h;
      menuChoice = ((int32_t)menu_id << 16) | (menu_itm & 0xFFFF);
      compactheap();
      HandleMenuChoice();
      if (revertgame || (wascombat && !incombat)) {
        itemHit = 2; /* "no" */
        goto out;
      }
      SetPortDialogPort(question);
      continue;
    }

    if (IsDialogEvent(&gTheEvent)) {
      if (DialogSelect(&gTheEvent, &dummy, &itemHit))
        goto out;
    }

    if (gTheEvent.what == keyDown) {
      switch (gTheEvent.message & charCodeMask) {
        case '\r': /**** Return — confirm Yes ****/
        case '\003': /**** Enter — confirm Yes ****/
        case 'y': /**** yes ****/
        case 'Y': /**** yes ****/
          itemHit = 3;
          goto out;
          break;

        case 'n': /**** no ****/
        case 'N': /**** no ****/
          itemHit = 2;
          goto out;
          break;
      }
    }
  }

out:
  SetCCursor(sword);
  GetDialogItem(question, itemHit, &itemType, &itemHandle, &buttonrect);
  ploticon3(133, buttonrect);
  delay(2);
  ploticon3(134, buttonrect);
  DisposeDialog(question);
  SetPort(oldport);
  if (doin)
    out();
  return (itemHit - 2);
}

/***************************** question2 ********************************/
short question2(short prompt1, short prompt2) {
  FILE* fp = NULL;
  short win = 200;
  GrafPtr oldPort;
  char keyhit = 0;
  Str255 one, two;
  char onechar = 'y';
  char twochar = 'n';
  Boolean wascombat = incombat;

  DialogRef question, dummy;

  compactheap();
  sound(20005);
  GetPort(&oldPort);
  SetCCursor(ask);
  if (prompt1) {
    GetIndString(one, 3, 1);
    GetIndString(two, 3, 1);

    getfilename("Data OD");
    if ((fp = MyrFopen(filename, "rb")) == NULL) {
      getfilename("Data SD2");
      if ((fp = MyrFopen(filename, "rb")) == NULL)
        scratch(134);

      fseek(fp, abs(prompt1) * sizeof myString, SEEK_SET);
      fread(&one, sizeof myString, 1, fp);
      fseek(fp, abs(prompt2) * sizeof myString, SEEK_SET);
      fread(&two, sizeof myString, 1, fp);
      fclose(fp);
      PtoCstr(one); // Myriad //**** Fantasoft 7.1  MyrPtoCstr seems to do nothing?
      PtoCstr(two); // Myriad //**** Fantasoft 7.1  MyrPtoCstr seems to do nothing?
      MyrParamText((Ptr)one, (Ptr)two, (Ptr) "", (Ptr) "");
      win = 136;

    } else {

      fseek(fp, abs(prompt1) * 25, SEEK_SET);
      fread(&one, 25, 1, fp);
      fseek(fp, abs(prompt2) * 25, SEEK_SET);
      fread(&two, 25, 1, fp);
      fclose(fp);
      PtoCstr(one); // Myriad //**** Fantasoft 7.1  MyrPtoCstr seems to do nothing?
      PtoCstr(two); // Myriad //**** Fantasoft 7.1  MyrPtoCstr seems to do nothing?
      one[24] = two[24] = 0; // Fantasoft 7.1 Fixes a problem with crashes on Mac
      MyrParamText((Ptr)one, (Ptr)two, (Ptr) "", (Ptr) "");
      MyrParamText((Ptr)one, (Ptr)two, (Ptr) "", (Ptr) "");
      win = 136;
    }
  }
  question = GetNewDialog(win, 0L, (WindowPtr)-1L);
  SetPortDialogPort(question);
  BackPixPat(base);
  TextFont(defaultfont);
  ForeColor(yellowColor);

  MoveWindow(GetDialogWindow(question), GlobalLeft + 322 + leftshift, GlobalTop + 270 + downshift, FALSE);
  ShowWindow(GetDialogWindow(question));
  ErasePortRect();

  if (prompt1) {
    onechar = tolower(one[1]);
    twochar = tolower(two[1]);
  }

  FlushEvents(everyEvent, 0);

  for (;;) {
    WaitNextEvent(everyEvent, &gTheEvent, 0L, 0L);
#ifdef PC // Myriad
    DoCorrectBugMADRepeat();
#endif

    /* Keep the whole menu bar working while this prompt blocks the main loop
     * (fullscreen, load/revert, end adventure, view allies/bestiary, ...). Menu
     * interactions arrive as a synthetic mouseDown with both coordinates negative.
     * If the choice tears the game down (revert/load, or the adventure ended),
     * answer "no" so the caller unwinds. */
    if (gTheEvent.what == mouseDown && gTheEvent.where.v < 0 && gTheEvent.where.h < 0) {
      short menu_id = -gTheEvent.where.v;
      short menu_itm = -gTheEvent.where.h;
      menuChoice = ((int32_t)menu_id << 16) | (menu_itm & 0xFFFF);
      compactheap();
      HandleMenuChoice();
      if (revertgame || (wascombat && !incombat)) {
        itemHit = 1; /* "no" */
        goto out;
      }
      SetPortDialogPort(question);
      continue;
    }

    if (IsDialogEvent(&gTheEvent)) {
      if (DialogSelect(&gTheEvent, &dummy, &itemHit))
        goto out;
    }

    if (gTheEvent.what == keyDown) {
      keyhit = BitAnd(gTheEvent.message, charCodeMask);

      if (keyhit == twochar) {
        itemHit = 1;
        goto out;
      }

      if (keyhit == onechar) {
        itemHit = 2;
        goto out;
      }
    }
  }

out:

  SetCCursor(sword);
  GetDialogItem(question, itemHit, &itemType, &itemHandle, &buttonrect);
  downbutton(FALSE);
  GetPortBounds(GetQDGlobalsThePort(), &itemRect);
  // itemRect = qd.thePort->portRect;
  DisposeDialog(question);
  SetPort(oldPort);
  if (!incombat)
    updatefat(TRUE, 0, FALSE);
  else
    updatefat(FALSE, 0, TRUE);
  return (itemHit - 1);
}

/***************************** question3 ********************************/
short question3(Str255 p1, Str255 p2, Boolean showcancel) {
  FILE* fp = NULL;
  short win = 200;
  GrafPtr oldPort;
  unsigned char keyhit = 0;
  unsigned char onechar;
  unsigned char twochar;
  DialogRef question, dummy;
  char prompt1[255];
  char prompt2[255];
  /* Optional onscreen Cancel button (PICT 144, the stop/hand icon), used by the
   * combat "Swap Positions / Attack Friend" prompt. Drawn on the combat screen at
   * the position measured from the top-left of the screen; clicking it is the same
   * as pressing Esc. The 50x50 region under it is saved so it can be erased on exit. */
  Rect cancelrect, cancellocal;
  GWorldPtr cancelsave = NIL;
  Boolean wascombat = incombat;

  /* *** CHANGED FROM ORIGINAL IMPLEMENTATION *** */
  /* Note (danapplegate): Another instance where BlockMove is called with a byteLength
   * constant of 255, but is passed static string literals of various lengths. question3
   * is only called with these string literals, so we know it's safe to call strlen here.
   */
  // Myriad : C format instead off pascal string
  // BlockMove(p1, prompt1, 255); // Myriad
  // BlockMove(p2, prompt2, 255); // Myriad
  BlockMove(p1, prompt1, strlen((const char*)p1) + 1);
  BlockMove(p2, prompt2, strlen((const char*)p2) + 1);
  /* *** END CHANGES *** */
  onechar = prompt1[0];
  twochar = prompt2[0];
  CtoPstr(prompt1); // Myriad
  CtoPstr(prompt2); // Myriad

  compactheap();
  sound(20005);
  GetPort(&oldPort);
  SetCCursor(ask);

  if (prompt1) {
    ParamText((StringPtr)prompt1, (StringPtr)prompt2, (StringPtr) "", (StringPtr) "");
    win = 136;
  }

  question = GetNewDialog(win, 0L, (WindowPtr)-1L);
  SetPortDialogPort(question);
  BackPixPat(base);
  TextFont(defaultfont);
  ForeColor(yellowColor);

  MoveWindow(GetDialogWindow(question), GlobalLeft + 322 + leftshift, GlobalTop + 270 + downshift, FALSE);
  ShowWindow(GetDialogWindow(question));
  ErasePortRect();

  if (showcancel) {
    /* Local coords on the combat screen window (whose top-left is at GlobalLeft,
     * GlobalTop), so the button lands 616 px right / 352 px down from the screen. */
    SetRect(&cancelrect, 616 - GlobalLeft, 327 - GlobalTop,
            616 - GlobalLeft + 50, 327 - GlobalTop + 50);
    SetRect(&cancellocal, 0, 0, 50, 50);
    SetPort(GetWindowPort(screen));
    if (!NewGWorld(&cancelsave, 8, &cancellocal, 0L, 0L, 0L)) {
      CopyBits(GetPortBitMapForCopyBits(GetWindowPort(screen)),
               GetPortBitMapForCopyBits(cancelsave), &cancelrect, &cancellocal, 0, NIL);
    }
    pict(144, cancelrect);
    SetPortDialogPort(question);
  }

  onechar = tolower(onechar);
  twochar = tolower(twochar);

  FlushEvents(everyEvent, 0);

  for (;;) {
    WaitNextEvent(everyEvent, &gTheEvent, 0L, 0L);
#ifdef PC // Myriad
    DoCorrectBugMADRepeat();
#endif

    /* Menu-bar interactions arrive as a synthetic mouseDown with both coordinates
     * negative (see EventManager::push_menu_event). Keep the whole menu bar working
     * while this prompt blocks the main loop: fullscreen toggle, load/revert, end
     * adventure, view allies/bestiary, etc. If the choice tears the game down
     * (revert/load, or the adventure ended), cancel the prompt so combat unwinds. */
    if ((gTheEvent.what == mouseDown) && (gTheEvent.where.v < 0) && (gTheEvent.where.h < 0)) {
      short menu_id = -gTheEvent.where.v;
      short menu_itm = -gTheEvent.where.h;
      menuChoice = ((int32_t)menu_id << 16) | (menu_itm & 0xFFFF);
      compactheap();
      HandleMenuChoice();
      if (revertgame || (wascombat && !incombat)) {
        keyhit = 27; /* treat as Esc/cancel */
        itemHit = 1;
        goto out;
      }
      if (showcancel && cancelsave) {
        /* A view dialog (bestiary/allies) redraws the combat screen and wipes the
         * Cancel button, which was drawn onto that screen. Redraw it. The saved
         * background under it is unchanged, so no re-save is needed. */
        SetPort(GetWindowPort(screen));
        pict(144, cancelrect);
      }
      SetPortDialogPort(question);
      continue;
    }

    keyhit = 0;
    if (gTheEvent.what == keyDown) {
      keyhit = BitAnd(gTheEvent.message, charCodeMask);

      if (keyhit == 27) /**** esc key ****/
      {
        itemHit = 1;
        goto out;
      }

      if (keyhit == twochar) {
        itemHit = 2;
        goto out;
      }

      if (keyhit == onechar) {
        itemHit = 3;
        goto out;
      }
    }
    if (showcancel && (gTheEvent.what == mouseDown)) {
      Point cp = gTheEvent.where;
      SetPort(GetWindowPort(screen));
      GlobalToLocal(&cp);
      SetPortDialogPort(question);
      if (PtInRect(cp, &cancelrect)) {
        keyhit = 27; /* treat the Cancel button exactly like the Esc key */
        itemHit = 1;
        goto out;
      }
    }
    if (IsDialogEvent(&gTheEvent)) {
      if (DialogSelect(&gTheEvent, &dummy, &itemHit))
        goto out;
    }
  }

out:

  SetCCursor(sword);
  GetDialogItem(question, itemHit, &itemType, &itemHandle, &buttonrect);
  downbutton(FALSE);
  GetPortBounds(GetQDGlobalsThePort(), &itemRect);
  // itemRect = qd.thePort->portRect;
  DisposeDialog(question);
  if (cancelsave) {
    /* Erase the Cancel button by restoring the pixels saved from under it. */
    SetPort(GetWindowPort(screen));
    CopyBits(GetPortBitMapForCopyBits(cancelsave), GetPortBitMapForCopyBits(GetWindowPort(screen)),
             &cancellocal, &cancelrect, 0, NIL);
    DisposeGWorld(cancelsave);
  }
  SetPort(oldPort);
  if (!incombat)
    updatefat(TRUE, 0, FALSE);
  else
    updatefat(FALSE, 0, TRUE);
  if (keyhit)
    return (itemHit - 1);
  else {
    if (itemHit == 1) {
      itemHit = 2;
      return (1);
    } else {
      itemHit = 3;
      return (2);
    }
  }
}
