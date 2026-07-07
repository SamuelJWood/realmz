#include "realmzbuild.h"
#include "variables.h"
#include "../MusicManager.h"

/*************************************************************
                                RedrawAllRealmz
Myriad
Redraw all the Realmz's screen.
=> Nothing
<= Nothing
***********************************************************/
void RedrawAllRealmz(void) {
  if (FrontWindow() == look)
    updatemain(FALSE, 0);
  if ((indung) && (viewtype == 1)) {
    updatemain(FALSE, 0);
    UpdateWindow(FALSE);
  }
}

/*************************************************************
                                DoFreeBeforeQuit
Myriad
Free all necessary before quitting, stop music, clear temporary files
=> Nothing
<= 0 si not quit, 1 si quit
***********************************************************/
void DoFreeBeforeQuit(void) {
  MyrRemove(":Data Files:Data I1");
  MyrRemove(":Data Files:Data I2");
  MyrRemove(":Data Files:CTD3");
  MyrRemove(":Data Files:CE");
  MyrRemove(":Data Files:CLD");
  MyrRemove(":Data Files:CE2");
  MyrRemove(":Data Files:CL");
  MyrRemove(":Data Files:DN");
  MyrRemove(":Data Files:WN");
  MyrRemove(":Data Files:CD");
  MyrRemove(":Data Files:CT");
  MyrRemove(":Data Files:CS");

  if (charnum > -1) {
    for (t = 0; t <= charnum; t++)
      savecharacter(t);
  }

  if ((!nofade) && (!nologo)) {
    sound(26260);
    showlogo(90);
  }

  if (!nofade && !nologo)
    fadeinout(25, fadeout);

  for (t = 0; t < 60; t++) {
    if (FrontWindow() != NIL)
      DisposeWindow(FrontWindow());
  }

  testdevice = GetMainDevice();

  if (currentDepth != ((**(**testdevice).gdPMap)).pixelSize) {
    SetDepth(curGDev, currentDepth, 4, 8); /*** restore color depth to monitor if need be ***/
  }

  temp = resetvolume;
  SetSoundVol(temp);

  // DoFreeBeforeQuit() is always immediately followed by ExitToShell(), so we
  // deliberately leave the screen black after the logo's fade-to-black instead
  // of fading back in. The original Mac faded in here to restore the display
  // gamma before returning to the Finder; our SDL window is simply destroyed on
  // exit, so a fade-in would only flash the empty screen (and menu bar) for a
  // frame before quitting.

  FlushEvents(everyEvent, 0);
  return;
}

/*************************************************************
                                DoQuitRealmz
Myriad
ASk the user for quitting realmz
=> Nothing
<= 0 si not quit, 1 si quit
***********************************************************/
short DoQuitRealmz(void) {
  if (!background) {
    if (incombat) {
      if (!question(10))
        return (0);
    } else if (question(1))
      save(0);
  }

  /* *** CHANGED FROM ORIGINAL IMPLEMENTATION ***
   * NOTE(fuzziqersoftware): We no longer use the Data CD file to keep track
   * of character files; instead, we enumerate the Character Files directory.
   */
  /* setfileinfo("CDat", ":Character Files:Data CD"); */

  DoFreeBeforeQuit();
  return (1);
}

/* Auto-journal helpers (used by the Maps/Notes -> Journal command below).
 * The Journal shows seen messages in the order they appeared, numbered
 * sequentially from 1; `view` is that 1-based note number. The text is read
 * from the scenario string file by the recorded index, so only messages the
 * party has actually seen are ever shown. */
static void showjournalnote(FILE* journalfp, short view) {
  if ((view >= 1) && (view <= notecount)) {
    GetIndString(myString, 3, 1);
    fseek(journalfp, noteorder[view - 1] * sizeof myString, SEEK_SET);
    fread(myString, sizeof myString, 1, journalfp);
    MyrPascalDiStr(2, myString);
    DialogNum(6, view);
  } else {
    /* Empty journal: blank body and note number 0 (never an arbitrary page). */
    MyrCDiStr(2, (StringPtr) "");
    DialogNum(6, 0);
  }
}

/* Left/Right arrow keys page to the previous/next note, mirroring the
 * on-screen arrow buttons (items 4 and 5). */
static Boolean journal_filter(DialogPtr dlg, EventRecord* ev, short* hit) {
  (void)dlg;
  if (ev->what == keyDown) {
    uint8_t vkey = (uint8_t)((ev->message >> 8) & 0xFF);
    if (vkey == 0x7B) {
      *hit = 4;
      return TRUE;
    } /* Left  -> previous note */
    if (vkey == 0x7C) {
      *hit = 5;
      return TRUE;
    } /* Right -> next note     */
  }
  return FALSE;
}

/***************************** HandleMenuChoice ********************************/
short HandleMenuChoice(void) {
  CCrsrHandle edit;
  FILE* fp = NULL;
  int32_t noteid;
  short notenum, orignoteid, temp, accNumber, tempdx, tempdy;
  short temppartyx, temppartyy, templevel, tempdunglevel, tempdung, tempx, tempy;
  Rect mainrect;

  theMenu = HiWord(menuChoice);
  theItem = LoWord(menuChoice);
  HiliteMenu(0);

  switch (theMenu) {
    case 139:
      if ((divine) || (development)) {
        if (usercheck2()) {
          switch (theItem) {
            case 1:
              if (FrontWindow() != GetDialogWindow(background)) {
                reply = indung;
                warp();
                if ((indung) && (!reply))
                  threed(dunglevel, floorx, floory, 1);
                updatemain(FALSE, 0);
              }
              break;

            case 2:
              editstring();
              break;

            case 3:

              for (t = 0; t <= charnum; t++) {
                c[t].money[0] += 200;
                c[t].load += 200;
                if (c[t].spellpointsmax)
                  c[t].spellpointsmax = c[t].spellpoints = 400;
                for (ttt = 0; ttt < 7; ttt++) {
                  c[t].nspells[ttt] = 0;
                  for (tt = 0; tt < 12; tt++)
                    c[t].cspells[ttt][tt] = TRUE;
                }
              }

              break;

            case 4: /**** levelup ****/
              for (t = 0; t <= charnum; t++) {
                c[t].exp = 1;
              }
              break;

            case 5: /**** special ****/

              break;

            case 6:

              killmon = numenemy;
              for (t = 0; t < maxmon; t++) {
                monster[t].stamina = 1;
                monster[t].traiter = 0;
                for (tt = 0; tt < 40; tt++)
                  monster[t].condition[tt] = 0;
              }

              break;

            case 7:
              for (t = 0; t <= charnum; t++)
                c[t].exp = 1;
              break;
          }
        }
      }
      break;

    case 136: /***************** character menu ****************/
      switch (theItem) {
        case 1:
          order(); /********** party order **********/
          break;

        case 2:
          modify(1); /********** new portrait **********/
          if ((indung) && (viewtype == 1))
            UpdateWindow(FALSE);
          break;

        case 3:
          modify(2); /********** new icon **********/
          if ((indung) && (viewtype == 1))
            UpdateWindow(FALSE);
          break;

        case 5: /********** new character **********/
          characterl.race = characterl.caste = characterl.gender = 0;
          DisableItem(gApple, 0);
          DisableItem(gFile, 0);
          DisableItem(gGame, 0);
          DisableItem(gParty, 0);
          DisableItem(gBeast, 0);
          DisableItem(gNPC, 0);
          DisableItem(gScenario, 0);
          DisableItem(prefer, 0);
          DrawMenuBar();
          CharacterGen();
          EnableItem(gApple, 0);
          EnableItem(gFile, 0);
          EnableItem(gGame, 0);
          EnableItem(gParty, 0);
          EnableItem(prefer, 0);
          DrawMenuBar();
          break;

        case 7: /********** alter party **********/
          partyselect(1);
          updatemain(FALSE, 0);
          if ((indung) && (viewtype == 1))
            UpdateWindow(FALSE);
          warn(20);
          break;
      }
      break;

    case 146: /***************** Allies menu ****************/
      in();
      beast(0, 1, holdover[theItem - 1].name);
      goto redobeast;
      break;

    case 132:
      in();

      beast(theItem, 1, 0);

    redobeast:

      if ((indung) && (viewtype == 1))
        UpdateWindow(FALSE);

    updatescreen:

      if (charnum > -1)
        updatemain(FALSE, -1);
      else
        DrawDialog(background);
      if (incombat) {
        centerstage(0);
        combatupdate2(lastshown);
        updatecontrols();
      }
      break;

    case 128:
      in();

      switch (theItem) {
        case 1:
          /******** put about here ***********/
          aboutrealmz();
          break;

        case 2: /**** About Fantasoft *****/
          movie(1128, 129, 1);
          break;

        case 3: /**** About Prelude *****/
          movie(1130, 129, 0);
          break;

        case 4: /**** About Assault *****/
          movie(1129, 129, 0);
          break;

        case 5: /**** About Destroy *****/
          movie(1132, 129, 0);
          break;

        case 6: /**** About Castle *****/
          movie(1131, 129, 0);
          break;

        case 7: /**** About Grilochs Dragon *****/
          movie(1134, 129, 0);
          break;

        case 8: /**** About White Dragon *****/
          movie(1133, 129, 0);
          break;

        case 9: /**** About Mithril Vault *****/
          movie(1136, 129, 0);
          break;

        case 10: /**** Twin Sands of Time *****/
          movie(1138, 129, 0);
          break;

        case 11: /**** Trouble in the Sword Lands *****/
          movie(1135, 129, 0);
          break;

        case 12: /**** War in the Sword Lands *****/
          movie(1137, 129, 0);
          break;

        case 13: /**** Wrath of the Mind Lords *****/
          movie(1139, 129, 0);
          break;

        case 14: /**** About Half Truth *****/
          movie(1140, 128, 0);
          break;

        default: /*** launch something from apple menu ***/
          GetMenuItemText(gApple, theItem, myString);
          accNumber = OpenDeskAcc(myString);
          break;
      }

    updateaftermovie:

      if (incombat) {
        updatemain(TRUE, 0);
        combatupdate2(charup);
        centerfield(pos[charup][0], pos[charup][1]);
        updatecontrols();
        if (inspell)
          drawspell();
        FlushEvents(everyEvent, 0);
      } else if (FrontWindow() == look)
        updatemain(FALSE, 0);
      else if ((indung) && (viewtype == 1)) {
        updatemain(FALSE, 0);
        UpdateWindow(FALSE);
      } else if (FrontWindow() == GetDialogWindow(background))
        DrawDialog(background);
      break;

    case 134: /********* speed menu **********/

      SetItemMark(gSpeed, oldspeed, 0);
      SetItemMark(gSpeed, theItem, 19);
      oldspeed = theItem;

      delayspeed = theItem * 5 - 5;

      savepref();

      break;

    case 135: /********* volume menu **********/

      if (twixt(theItem, 2, 9)) //** set Sound FX volume
      {
        SetItemMark(gSound, volume + 2, 0);
        SetItemMark(gSound, theItem, 19);

        volume = theItem - 2;
        SetSoundVol(volume);
        sound(642);
      }

      savepref();

      break;

    case 147: /********* music volume menu **********/

      if (twixt(theItem, 2, 9)) {
        CheckItem(gMusicVol, musicvolume + 2, FALSE);
        musicvolume = theItem - 2;
        CheckItem(gMusicVol, musicvolume + 2, TRUE);
        MusicManager_SetVolume(musicvolume);
        savepref();
      }

      break;

    case 137: /*********  preferences **************/

      switch (theItem) {
        case 1: /****** Toggle Fullscreen *********/
          WindowManager_ToggleFullscreen();
          CheckItem(prefer, 1, WindowManager_IsFullscreen());
          break;

        case 5: /****** Toggle Hurry Spell Resolution *********/

          if (quickshow)
            quickshow = FALSE;
          else
            quickshow = TRUE;

          CheckItem(prefer, 5, quickshow);
          break;
      }

      break;

    case 129: /************** file menu **************/
      switch (theItem) {
        case 8: /*********** quit realmz ***********/
          if (DoQuitRealmz())
            ExitToShell();
          break;

        case 3: /************ save current game ************/
          save(0);
          if ((indung) && (viewtype == 1))
            UpdateWindow(FALSE);
          break;

        case 4: /************ fast save current game ************/
          save(-1);
          break;

        case 2: /************ fast revert to previous game ************/

          if (!load()) {
            /* Load was cancelled. During combat the exact scene (in-flight
             * spell frames, floating damage numbers, ...) is still held in the
             * look window's pixmap behind the now-disposed dialog, and disposing
             * the dialog already recomposited it back. A logical redraw here
             * (centerstage/in) would rebuild the scene and lose those transient
             * overlays, so skip it during combat -- on either the player's or
             * the computer's turn -- and let combat resume from exactly where it
             * paused. */
            if (incombat) {
              /* preserved scene shows through; nothing to redraw */
            } else {
              centerpict();
              in();
            }
          } else {
            revertgame = TRUE;
            goto playsaved;
          }
          break;

        case 1: /************ play saved game ************/

          if (!load()) {
            MyrCDiStr(2, (StringPtr) "");
            MyrCDiStr(3, (StringPtr) "");
            return (0);
          }
        playsaved:
          EnableItem(gScenario, 0);
          EnableItem(gParty, 0);
          EnableItem(gParty, 1);
          EnableItem(gParty, 2);
          EnableItem(gParty, 3);
          EnableItem(gParty, 7);
          DisableItem(gParty, 5);
          EnableItem(gGame, 3);
          DisableItem(gGame, 1);
          EnableItem(gBeast, 0);
          EnableItem(gFile, 3);
          EnableItem(gFile, 4);
          EnableItem(gFile, 2);
          DisableItem(gFile, 1);
          TextFont(defaultfont);
          DrawMenuBar();
          if (partycondition[PARTY_COND_TORCH_LIT])
            loaddark((partycondition[PARTY_COND_TORCH_LIT] / 30) + 1);
          else
            loaddark(0);

          if (!revertgame)
            mainscreeninit(0, 0);
          else
            return (0);
          break;
      }
      break;

    case 200: /**** maps *****/
      if (theItem > 2) {
        CheckItem(gScenario, theItem, 0);
        showmap(theItem - 4);
      } else if (theItem == 1) /********* notes *************/
      {
        if (indung)
          saveland(dunglevel);
        else
          saveland(landlevel);

        edit = GetCCursor(155);
        SetCCursor(edit);
        DisposeCCursor(edit);
        if (!reducesound)
          sound(647);
        innotes = TRUE;
        info2 = GetNewDialog(164 + (1000 * screensize), 0L, (WindowPtr)-1L);
        SetPortDialogPort(info2);
        BackPixPat(base);
        gCurrent = info2;
        TextFont(defaultfont);
        ForeColor(yellowColor);
        GetDialogItem(info2, 2, &itemType, &itemHandle, &itemRect);
        MoveWindow(GetDialogWindow(info2), GlobalLeft, GlobalTop + 321 + downshift, FALSE);
        ShowWindow(GetDialogWindow(info2));
        ErasePortRect();

        temppartyx = partyx;
        temppartyy = partyy;
        tempdung = indung;
        templevel = landlevel;
        tempdunglevel = dunglevel;
        tempx = lookx;
        tempy = looky;
        tempdx = floorx;
        tempdy = floory;

        if (indung) {
          if ((fp = MyrFopen(":Data Files:DN", "r+b")) == NULL)
            scratch(61);
          noteid = 10000 * dunglevel + floorx * 90 + floory;
        } else {
          if ((fp = MyrFopen(":Data Files:WN", "r+b")) == NULL)
            scratch(62);
          noteid = 10000 * landlevel + ((lookx + partyx) * 90) + looky + partyy;
        }

        notenum = -1;
        orignoteid = -1;

        do {
          notenum++;
          orignoteid++;

          if (notenum > 500)
            goto newnote;

          if (!fread(&note, sizeof note, 1, fp))
            goto newnote;
          CvtNoteToPc(&note);
          if (!StringWidth((StringPtr)note.string)) {
            if (!MyrBitTstShort((void*)(&field[partyx + lookx][partyy + looky]), 1)) {
              note.id = noteid;
              if (indung) {
                note.isdung = TRUE;
                note.level = dunglevel;
                note.wasdark = FALSE;
                note.x = floorx;
                note.y = floory;
              } else {
                note.isdung = FALSE;
                note.level = landlevel;
                if (randlevel.isdark)
                  note.wasdark = partycondition[PARTY_COND_TORCH_LIT] / 30 + 1;
                else
                  note.wasdark = 0;
                note.x = lookx + partyx;
                note.y = looky + partyy;
              }
              goto gotnote;
            }
          }
        } while (note.id != noteid);

        goto gotnote;

      newnote:

        fclose(fp);
        note.id = noteid;
        if (indung) {
          note.isdung = TRUE;
          note.level = dunglevel;
          note.wasdark = FALSE;
          note.x = floorx;
          note.y = floory;
        } else {
          note.isdung = FALSE;
          note.level = landlevel;
          if (randlevel.isdark)
            note.wasdark = partycondition[PARTY_COND_TORCH_LIT] / 30 + 1;
          else
            note.wasdark = 0;
          note.x = lookx + partyx;
          note.y = looky + partyy;
        }

        if (indung) {
          if ((fp = MyrFopen(":Data Files:DN", "a+b")) == NULL)
            scratch(63);
        } else {
          if ((fp = MyrFopen(":Data Files:WN", "a+b")) == NULL)
            scratch(64);
        }

        GetDialogItem(info2, 2, &itemType, &itemHandle, &itemRect);
        GetDialogItemText(itemHandle, note.string);

      gotnote:
        GetDialogItem(info2, 2, &itemType, &itemHandle, &itemRect);
        SetDialogItemText(itemHandle, note.string);
        itemHit = 0;
        indung = note.isdung;
        dunglevel = landlevel = note.level;
        templong = note.level;
        loadland(templong, TRUE);

        DialogNum(6, notenum + 1);

        if (note.wasdark)
          loaddark(note.wasdark);
        else
          loaddark(0);

        lookx = looky = 0;
        floorx = partyx = note.x;
        floory = partyy = note.y;
        SetPort(GetWindowPort(look));
        centerpict();
        SelectWindow(GetDialogWindow(info2));
        SetPortDialogPort(info2);

        while ((itemHit != 1) && (itemHit != 3)) {
          FlushEvents(everyEvent, 0);
          ModalDialog(0L, &itemHit);

          if ((itemHit == 2) && (orignoteid != notenum)) /*** reset note for editing ***/
          {
            MyrCDiStr(2, (StringPtr) "Sorry....You can only edit a note you are standing on.");
            sound(6000);
            delay(60);
            MyrPascalDiStr(2, note.string);
          }

          if ((itemHit == 4) || (itemHit == 5)) {
            SetPortDialogPort(info2);
            GetDialogItem(info2, itemHit, &itemType, &itemHandle, &itemRect);
            ploticon3(135, itemRect);
            sound(141);

            if (notenum == orignoteid) /**** save original note ****/
            {
              fclose(fp);
              fp = NIL;

              if (indung) {
                if ((fp = MyrFopen(":Data Files:DN", "r+b")) == NULL)
                  scratch(63);
              } else {
                if ((fp = MyrFopen(":Data Files:WN", "r+b")) == NULL)
                  scratch(64);
              }

              GetDialogItem(info2, 2, &itemType, &itemHandle, &itemRect);
              GetDialogItemText(itemHandle, note.string);
              fseek(fp, notenum * sizeof note, SEEK_SET);
              CvtNoteToPc(&note);
              if (!fwrite(&note, sizeof note, 1, fp))
                scratch(65);
              CvtNoteToPc(&note);
            }

            notenum--;
            if (itemHit == 5)
              notenum += 2;
            if (notenum < 0)
              notenum = 0;

          backupabit:

            fseek(fp, notenum * sizeof note, SEEK_SET);

            if (!fread(&note, sizeof note, 1, fp)) {
              notenum--;
              goto backupabit;
            }
            CvtNoteToPc(&note);
            GetDialogItem(info2, itemHit, &itemType, &itemHandle, &itemRect);
            ploticon3(136, itemRect);
            goto gotnote;
          }
        }

        innotes = FALSE;
        indung = tempdung;
        landlevel = templevel;
        dunglevel = tempdunglevel;
        lookx = tempx;
        looky = tempy;
        floorx = tempdx;
        floory = tempdy;
        partyx = temppartyx;
        partyy = temppartyy;

        if (indung)
          loadland(dunglevel, TRUE);
        else
          loadland(landlevel, TRUE);

        if (randlevel.isdark)
          loaddark(partycondition[PARTY_COND_TORCH_LIT] / 30 + 1);

        SetPortDialogPort(info2);
        GetDialogItem(info2, 3, &itemType, &itemHandle, &itemRect);
        ploticon3(129, itemRect);
        sound(141);

        if (notenum == orignoteid) /**** save note only if on original ****/
        {
          GetDialogItem(info2, 2, &itemType, &itemHandle, &itemRect);
          GetDialogItemText(itemHandle, note.string);

          if (indung) {
            if (StringWidth(note.string))
              MyrBitSetShort(&field[floory][floorx], 10);
            else
              MyrBitClrShort(&field[floory][floorx], 10);
          } else {
            if (StringWidth(note.string))
              MyrBitSetShort(&field[partyx + lookx][partyy + looky], 1);
            else
              MyrBitClrShort(&field[partyx + lookx][partyy + looky], 1);
          }

          fseek(fp, notenum * sizeof note, SEEK_SET);

          if (indung) {
            note.isdung = TRUE;
            note.level = dunglevel;
            note.wasdark = FALSE;
            note.x = floorx;
            note.y = floory;
          } else {
            note.isdung = FALSE;
            note.level = landlevel;
            if (randlevel.isdark)
              note.wasdark = partycondition[PARTY_COND_TORCH_LIT] / 30 + 1;
            else
              note.wasdark = 0;
            note.x = lookx + partyx;
            note.y = looky + partyy;
          }

          if (!StringWidth((StringPtr)note.string))
            note.id = 0;

          CvtNoteToPc(&note);
          if (!fwrite(&note, sizeof note, 1, fp))
            scratch(65);
          CvtNoteToPc(&note);
          if (indung) {
            saveland(dunglevel);
            loadland(dunglevel, 0);
          } else
            saveland(landlevel);
        }

      blownotes:
        fclose(fp);
      blownotes2:
        DisposeDialog(info2);
        SetCCursor(sword);
        SetPort(GetWindowPort(screen));

        if (screensize) {
          mainrect.top = 321 + downshift;
          mainrect.left = 0;
          mainrect.bottom = 555;
          mainrect.right = 308 + leftshift;
          pict(203, mainrect);
        } else {
          mainrect.top = 321 + downshift;
          mainrect.left = 0;
          mainrect.bottom = 460 + 96;
          mainrect.right = 308;
          pict(203, mainrect);
        }

        updatetorch();
        updatecontrols();
        centerpict();
        timeclick(0, FALSE);
        if (revertgame)
          return (0);
      } else /***** journal *****/
      {
        short jview; /* 1-based number of the note currently shown */
        Rect jpb;

        SetCCursor(sword);
        if (!reducesound)
          sound(647);

        info2 = GetNewDialog(164, 0L, (WindowPtr)-1L);
        SetPortDialogPort(info2);
        gCurrent = info2;
        ForeColor(yellowColor);
        BackPixPat(base);

        /* The note body and the note-number field are display-only: the player
         * cannot edit journaled text or type an arbitrary note number. */
        WindowManager_SetItemEditable(info2, 2, FALSE);
        WindowManager_SetItemEditable(info2, 6, FALSE);

        /* Center the Journal over the main overland view (window-local
         * 0,0..(320+leftshift),(320+downshift)). */
        GetPortBounds((CGrafPtr)GetDialogWindow(info2), &jpb);
        MoveWindow(GetDialogWindow(info2),
                   GlobalLeft + ((320 + leftshift) - (jpb.right - jpb.left)) / 2,
                   GlobalTop + ((320 + downshift) - (jpb.bottom - jpb.top)) / 2,
                   FALSE);
        ShowWindow(GetDialogWindow(info2));
        ErasePortRect();

        getfilename("Data SD2");
        if ((fp = MyrFopen(filename, "r")) == NULL) {
          flashmessage((StringPtr) "Could not open the Journal File.  Sorry.", 50, 50, 120, 5000);
          goto blownotes2;
        }

        /* Open on the most recent note so the player can page back through the
         * log. An empty journal shows a blank page numbered 0. */
        jview = notecount;
        showjournalnote(fp, jview);

        itemHit = 0;
        for (;;) {
          short newview = jview;
          FlushEvents(everyEvent, 0);
          ModalDialog(journal_filter, &itemHit);

          if ((itemHit == 1) || (itemHit == 3) || (itemHit == 0))
            break; /* OK / Done / Escape close the Journal */

          if (itemHit == 4)
            newview = jview - 1; /* previous note (left arrow)  */
          else if (itemHit == 5)
            newview = jview + 1; /* next note (right arrow)     */
          else
            continue;

          GetDialogItem(info2, itemHit, &itemType, &itemHandle, &itemRect);
          ploticon3(135, itemRect);

          if ((newview < 1) || (newview > notecount)) {
            sound(147); /* bump at the first/last note */
          } else {
            jview = newview;
            sound(141);
            showjournalnote(fp, jview);
          }

          GetDialogItem(info2, itemHit, &itemType, &itemHandle, &itemRect);
          ploticon3(136, itemRect);
        }
        goto blownotes;
      }

      break;

    case 130:
      switch (theItem) /**************** game menu ***********/
      {
        case 1: /******************** start new game ************/
          charnum = -1;
          fat = 4;
          reply = TRUE;

          if (currentscenario < 5)
            currentscenario = 5;

          if (currentscenario == 5)
            reply = FALSE;

          GetMenuItemText(gGame, currentscenario, myString);
          PtoCstr(myString);
          strcpy(filename, myString);

          if (selectscenario(filename, reply)) {
            monsterset = 0;
            partyselect(0);
            SetPortDialogPort(background);
            BackColor(blackColor);
            DrawDialog(background);
            if (charnum < 0) {
              DisableItem(gScenario, 0);
              return (0);
            }
            EnableItem(gBeast, 0);
            EnableItem(gScenario, 0);
            EnableItem(gParty, 0);
            EnableItem(gParty, 1);
            EnableItem(gParty, 2);
            EnableItem(gParty, 3);
            EnableItem(gParty, 7);
            DisableItem(gParty, 5);
            EnableItem(gFile, 3);
            EnableItem(gFile, 4);
            EnableItem(gFile, 2);
            DisableItem(gFile, 1);
            DisableItem(gGame, 1);
            EnableItem(gGame, 3);

            tyme.tm_sec = 0;
            tyme.tm_min = 0;
            tyme.tm_hour = 0;
            tyme.tm_mday = 1;
            tyme.tm_mon = 0;
            tyme.tm_year = 100;
            tyme.tm_yday = 1;
            tyme.tm_isdst = -1;

            setupnewgame();
            for (t = 0; t <= charnum; t++) {
              if (c[t].stamina < 1)
                killparty++;
              for (tt = 0; tt < 12; tt++) {
                lastspell[t][0] = 0;
                if (c[t].cspells[0][tt])
                  lastspell[t][1] = tt;
              }
            }

            getfilename("Global"); /******** run global start macro *****/
            if ((fp = MyrFopen(filename, "rb")) != NULL) {
              fread(&globalmacro, sizeof globalmacro, 1, fp);
              CvtTabShortToPc(globalmacro, 30);
              fclose(fp);
            }

            mainscreeninit(0, globalmacro[0]);

            return (TRUE);
          }
          break;

        case 3: /*********** end current game ***************/
          if (!incombat) {
            if (question(1))
              save(0);
          } else if (!question(10))
            return (0);

          for (t = 0; t <= charnum; t++)
            savecharacter(t);

          getfilename("Global"); /******** run global quit macro *****/
          if ((fp = MyrFopen(filename, "rb")) != NULL) {
            fread(&globalmacro, sizeof globalmacro, 1, fp);
            CvtTabShortToPc(globalmacro, 30);
            fclose(fp);

            if (globalmacro[2]) {
              updatemain(TRUE, -1);
              newland(0L, 0L, 1, globalmacro[2], 0);
            }
          }
          partyloss(0);

          break;

        default:
          if (FrontWindow() != GetDialogWindow(background)) {
            SetPort(GetWindowPort(look));
            warn(63);
            return (0);
          }

          if (theItem > 4) {
            GetMenuItemText(gGame, theItem, myString);
            PtoCstr(myString);
            strcpy(filename, myString);
          }

          if (doreg()) {
            if (selectscenario(filename, TRUE)) {
              CheckItem(gGame, currentscenario, FALSE);
              CheckItem(gGame, theItem, TRUE);
              currentscenario = theItem;
            }
          } else
            warn(138);
          break;
      }
      break;
  }
  return (0);
}
