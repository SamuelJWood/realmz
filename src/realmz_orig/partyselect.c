#include "prototypes.h"
#include "variables.h"

#include "files.h"

/* The small selection buttons (difficulty and monster set) all use the same
 * 13x13 graphic (PICT 179). Their DITL rects vary in size, which stretched the
 * graphic; shrink the rect to a centered 13x13 box so it draws undistorted. */
static void fit13(Rect* r) {
  short cx = (r->left + r->right) / 2;
  short cy = (r->top + r->bottom) / 2;
  r->left = cx - 6;
  r->right = cx + 7;
  r->top = cy - 6;
  r->bottom = cy + 7;
}

/* Centered 13x13 draw rect for a monster-set button. The Mega button (item 54)
 * sits one pixel left of the other two, so nudge it right to line them up. */
static void monster_btn_rect(DialogPtr dlg, short item, Rect* r) {
  GetDialogItem(dlg, item, &itemType, &itemHandle, r);
  fit13(r);
  if (item == 54) {
    r->left += 1;
    r->right += 1;
  }
}

/* Pseudo itemHit codes returned by partyselect_filter for keyboard navigation.
 * They sit well above any real DITL item number so they never collide. */
#define PS_KEY_DOWN 200
#define PS_KEY_UP 201
#define PS_KEY_ENTER 202

/* Translate mouse-wheel events into clicks on the list scroll buttons so the
 * wheel scrolls the character list one entry at a time (38 = up, 39 = down),
 * and map the arrow/Return keys to navigation pseudo-hits handled in the modal
 * loop (arrows move the file-list selection, Return/Enter adds or starts). */
static Boolean partyselect_filter(DialogPtr dlg, EventRecord* ev, short* hit) {
  (void)dlg;
  if (ev->what == app3Evt) {
    *hit = (ev->message == 1) ? 38 : 39; /* message 1 = scroll up, 0 = scroll down */
    return TRUE;
  }
  if ((ev->what == keyDown) || (ev->what == autoKey)) {
    uint8_t vkey = (uint8_t)((ev->message >> 8) & 0xFF);
    uint8_t ch = (uint8_t)(ev->message & 0xFF);
    if (vkey == 0x7D) { *hit = PS_KEY_DOWN; return TRUE; } /* down arrow */
    if (vkey == 0x7E) { *hit = PS_KEY_UP; return TRUE; }   /* up arrow   */
    if ((ch == 0x0D) || (ch == 0x03)) { *hit = PS_KEY_ENTER; return TRUE; } /* Return / Enter */
  }
  /* Clicking a difficulty-level or monster-set *label* selects the matching
   * button, just like clicking the small box itself. The labels aren't clickable
   * dialog items (the difficulty labels are one disabled multi-line text item;
   * the monster labels are hand-drawn), so hit-test the mouse against their
   * on-screen regions. GetMouse returns coordinates local to the party port. */
  if (ev->what == mouseDown) {
    Point pt;
    GetMouse(&pt);
    /* Difficulty labels (Novice/Easy/Normal/Hard/Veteran): one text item at
     * x 248..342, y 345..455, five evenly spaced lines mapping to boxes 80..84. */
    if ((pt.h >= 248) && (pt.h <= 342) && (pt.v >= 345) && (pt.v < 455)) {
      short line = (pt.v - 345) / 22;
      if (line < 0) line = 0;
      if (line > 4) line = 4;
      *hit = 80 + line;
      return TRUE;
    }
    /* Monster-set labels, drawn just to the right of buttons 55/56/54. */
    {
      short mbtn[3] = {55, 56, 54};
      short i;
      for (i = 0; i < 3; i++) {
        GetDialogItem(party, mbtn[i], &itemType, &itemHandle, &itemRect);
        if ((pt.h >= itemRect.right) && (pt.h <= itemRect.right + 150) &&
            (pt.v >= itemRect.top - 4) && (pt.v <= itemRect.bottom + 6)) {
          *hit = mbtn[i];
          return TRUE;
        }
      }
    }
  }
  return FALSE;
}

/* Redraw only the scrollable character-file list (name items 1..13 and the
 * matching level items 41..53). Used by the scroll and keyboard-navigation
 * paths so they don't repaint—and visibly flicker—the party display, the
 * selection marker, or the instruction text, none of which change there. */
static void partyselect_draw_list(short index, short filepick) {
  gCurrent = party;
  SetPortDialogPort(party);
  TextMode(1);
  TextFont(font);
  update_character_files_list();
  for (size_t z = 0; z < 13; z++) {
    const char* name;
    short level;
    get_character_info_from_list(index + z, &name, &level);
    short name_item_id = z + 1;
    short level_item_id = z + 41;
    if (name) {
      ForeColor((name_item_id == filepick) ? yellowColor : cyanColor);
      MyrCDiStr(name_item_id, (StringPtr)name);
      DialogNum(level_item_id, level);
    } else {
      MyrCDiStr(name_item_id, (StringPtr) "");
      MyrCDiStr(level_item_id, (StringPtr) "");
    }
  }
  ForeColor(yellowColor);
}

/*************************** partyselect *************************/
void partyselect(short mode) {
  short filepick, oldcharpick, newcharpick, t, index, level, totallevel;
  float total, required;
  Boolean danger = FALSE;
  Boolean hasrestrictions = FALSE;
  FILE* fp = NULL;
  char name[30];
  // SFTypeList types;
  Point where;

  forcesmall = TRUE;

  // strcpy(types,(StringPtr)"APPL");//*** not used but there to avoid the compile error ***/

  SetCCursor(sword);

  oldcharpick = newcharpick = index = filepick = totallevel = required = 0;
  needdungeonupdate = TRUE;

  party = GetNewDialog(145, 0L, (WindowPtr)-1L);
  if (!reducesound)
    sound(10004);
  SetPortDialogPort(party);
  gCurrent = party;
  /* Paint the textured background into the window before showing it, so it
   * doesn't briefly flash white before the first full redraw below. */
  BackPixPat(base);
  ErasePortRect();
  ForeColor(yellowColor);
  /* The DITL ships a single combined "Name  Level  Level" header whose words
   * don't line up with the columns. Clear it here so DrawDialog never draws it;
   * we draw our own column-aligned headers in shortupdate instead. */
  MyrCDiStr(90, (StringPtr) "");
  /* The monster-set labels ship as one wrapping text item that overflows its
   * rect; clear it and draw each label next to its button in bigupdate. */
  MyrCDiStr(57, (StringPtr) "");

  /* Layout tweaks to the party-select dialog:
   *  - The "Import" button (item 88) is no longer needed; move it off-screen
   *    so it is neither drawn by DrawDialog nor clickable.
   *  - The "View Restrictions" button (item 21) moves to roughly where Import
   *    used to be: 244 px left and 43 px up.
   *  - The "To add a character..." instructions (item 89) shift 45 px left. */
  {
    Rect r;
    GetDialogItem(party, 88, &itemType, &itemHandle, &r);
    SetRect(&r, -200, -200, -150, -150);
    WindowManager_SetDialogItemRect(party, 88, &r);

    GetDialogItem(party, 21, &itemType, &itemHandle, &r);
    r.left -= 244;
    r.right -= 244;
    r.top -= 43;
    r.bottom -= 43;
    WindowManager_SetDialogItemRect(party, 21, &r);

    GetDialogItem(party, 89, &itemType, &itemHandle, &r);
    r.left -= 45;
    r.right -= 45;
    WindowManager_SetDialogItemRect(party, 89, &r);

    /* Difficulty-Level area fine-tuning:
     *  - "Difficulty Level" header (item 77) shifts 10 px right.
     *  - Nudge the selection boxes vertically (Easy, item 81, stays put):
     *    Novice up 1, Normal down 1, Hard down 2, Veteran down 3. */
    GetDialogItem(party, 77, &itemType, &itemHandle, &r);
    r.left += 10;
    r.right += 10;
    WindowManager_SetDialogItemRect(party, 77, &r);

    GetDialogItem(party, 80, &itemType, &itemHandle, &r); /* Novice  */
    r.top -= 1;
    r.bottom -= 1;
    WindowManager_SetDialogItemRect(party, 80, &r);
    GetDialogItem(party, 82, &itemType, &itemHandle, &r); /* Normal  */
    r.top += 1;
    r.bottom += 1;
    WindowManager_SetDialogItemRect(party, 82, &r);
    GetDialogItem(party, 83, &itemType, &itemHandle, &r); /* Hard    */
    r.top += 2;
    r.bottom += 2;
    WindowManager_SetDialogItemRect(party, 83, &r);
    GetDialogItem(party, 84, &itemType, &itemHandle, &r); /* Veteran */
    r.top += 3;
    r.bottom += 3;
    WindowManager_SetDialogItemRect(party, 84, &r);

    /* Level-summary block alignment. Nudge the "= " labels so their equals
     * signs line up, pull the numbers in toward the labels, and raise the whole
     * Maximum/Recommended/Current group by 7 px. (Items: 76/77 = Maximum label
     * + number, 68/74 = Recommended label + number, 70/75 = Current label +
     * number.) */
    GetDialogItem(party, 76, &itemType, &itemHandle, &r); /* "Maximum Levels =" */
    r.left += 3;
    r.right += 3;
    r.top -= 7;
    r.bottom -= 7;
    WindowManager_SetDialogItemRect(party, 76, &r);

    GetDialogItem(party, 77, &itemType, &itemHandle, &r); /* Maximum number / "None" */
    r.left -= 38;
    r.right -= 38;
    r.top -= 7;
    r.bottom -= 7;
    WindowManager_SetDialogItemRect(party, 77, &r);

    GetDialogItem(party, 68, &itemType, &itemHandle, &r); /* "Recommended Levels =" */
    r.top -= 7;
    r.bottom -= 7;
    WindowManager_SetDialogItemRect(party, 68, &r);

    GetDialogItem(party, 74, &itemType, &itemHandle, &r); /* Recommended number */
    r.left -= 23;
    r.right -= 23;
    r.top -= 7;
    r.bottom -= 7;
    WindowManager_SetDialogItemRect(party, 74, &r);

    GetDialogItem(party, 70, &itemType, &itemHandle, &r); /* "Current Levels =" */
    r.left += 8;
    r.right += 8;
    r.top -= 7;
    r.bottom -= 7;
    WindowManager_SetDialogItemRect(party, 70, &r);

    GetDialogItem(party, 75, &itemType, &itemHandle, &r); /* Current number */
    r.left -= 23;
    r.right -= 23;
    r.top -= 7;
    r.bottom -= 7;
    WindowManager_SetDialogItemRect(party, 75, &r);

    /* Lower the "Experience Gained At" line (label, percentage, "%") by 4 px. */
    GetDialogItem(party, 71, &itemType, &itemHandle, &r); /* "Experience Gained At " */
    r.top += 4;
    r.bottom += 4;
    WindowManager_SetDialogItemRect(party, 71, &r);

    GetDialogItem(party, 72, &itemType, &itemHandle, &r); /* percentage number */
    r.top += 4;
    r.bottom += 4;
    WindowManager_SetDialogItemRect(party, 72, &r);

    GetDialogItem(party, 73, &itemType, &itemHandle, &r); /* "%" */
    r.top += 4;
    r.bottom += 4;
    WindowManager_SetDialogItemRect(party, 73, &r);
  }

  MoveWindow(GetDialogWindow(party), GlobalLeft + (leftshift / 2), GlobalTop + (downshift / 2), FALSE);
  ShowWindow(GetDialogWindow(party));

  /* While the party-selection screen is up, the Game, Adventure and Character
   * menus don't apply; gray them out. Preferences stays available. (Menu handle
   * names predate the current titles: gFile="Game", gGame="Adventure",
   * gParty="Character".) */
  DisableItem(gFile, 0);
  DisableItem(gGame, 0);
  DisableItem(gParty, 0);
  DrawMenuBar();

  /* *** CHANGED FROM ORIGINAL IMPLEMENTATION ***
  * NOTE(fuzziqersoftware): We no longer use the Data CD file to keep track
  * of character files; instead, we enumerate the Character Files directory.
  if ((fp = MyrFopen(":Character Files:Data CD", "rb")) == NULL) {
    if ((fp = MyrFopen(":Character Files:Data CD", "w+b")) == NULL)
      scratch2(10);
    fclose(fp);
    setfileinfo((Ptr) "CDat", (Ptr) ":Character Files:Data CD");
    flashmessage((StringPtr) "Your 'Data CD' file is damaged or missing. You need to IMPORT your characters.", 15, 60, 0, 6000);
    flashmessage((StringPtr) "To IMPORT your characters just click 'IMPORT' and locate your characters files.", 15, 60, 0, 6000);
  } else
    fclose(fp);
  */

  GetMenuItemText(gGame, currentscenario, myString);
  PtoCstr(myString);
  getfilename((Ptr)myString);
  if ((fp = MyrFopen(filename, "rb")) == NULL)
    scratch(130);
  /* *** CHANGED FROM ORIGINAL IMPLEMENTATION ***
   * See note in main.c about sizeof(long) vs. sizeof(int32_t). */
  fread(&reclevel, sizeof(int32_t), 1, fp);
  CvtLongToPc(&reclevel);
  fread(&maxlevel, sizeof(int32_t), 1, fp);
  CvtLongToPc(&maxlevel);
  /* *** END CHANGES *** */

  fclose(fp);

  /* *** CHANGED FROM ORIGINAL IMPLEMENTATION ***
   * NOTE(jpetrie): Originally, maxlevel was set to 999 here to indicate "no level cap" for the scenario. However, this
   * prevented extremely high level parties who legitimately had 1000 levels or more from starting a game, so now 0 is
   * used to indicate that there are no level restrictions.
   * (Incorporated from upstream Realmz-Castle/realmz PR #239.)
   */
  if (doreg())
    maxlevel = 0;

  charselectnew = 0;

  for (t = 0; t <= charnum; t++)
    c[t].inbattle = TRUE;

bigupdate:

  /* Batch the whole redraw into a single composite so the screen updates
   * atomically instead of flickering element-by-element (the menu/labels/
   * buttons were visibly blinking on drop and scroll). Recomposite is
   * re-enabled just before the modal loop, which composites once. */
  WindowManager_SetEnableRecomposite(0);

  SetPortDialogPort(party);
  BackPixPat(base);
  TextFont(font);
  ForeColor(yellowColor);
  TextSize(17);
  TextFace(bold);
  gCurrent = party;
  ErasePortRect();
  DrawDialog(party);

  for (t = 85; t < 88; t++) {
    GetDialogItem(party, t, &itemType, &itemHandle, &buttonrect);
    downbutton(FALSE);
  }
  GetDialogItem(party, 67, &itemType, &itemHandle, &buttonrect);
  downbutton(FALSE);
  GetDialogItem(party, 69, &itemType, &itemHandle, &buttonrect);
  downbutton(FALSE);
  GetDialogItem(party, 60, &itemType, &itemHandle, &buttonrect);
  downbutton(FALSE);

  for (t = 80; t < 85; t++) {
    GetDialogItem(party, t, &itemType, &itemHandle, &itemRect);
    SetDialogItemText(itemHandle, (StringPtr) "");
    fit13(&itemRect);
    pict(179, itemRect);
  }

  for (t = 54; t < 57; t++) {
    GetDialogItem(party, t, &itemType, &itemHandle, &itemRect);
    SetDialogItemText(itemHandle, (StringPtr) "");
    monster_btn_rect(party, t, &itemRect);
    pict(179, itemRect);
  }

  /* Monster-set heading and per-button labels (replacing the cleared item 57).
   * Each label is drawn on its own line just to the right of its button. */
  ForeColor(yellowColor);
  MoveTo(55, 340);
  MyrDrawCString("Use Monster Set");
  GetDialogItem(party, 55, &itemType, &itemHandle, &itemRect); /* monsterset 0 */
  MoveTo(itemRect.right + 4, itemRect.top + 12);
  MyrDrawCString("Normal Monsters");
  GetDialogItem(party, 56, &itemType, &itemHandle, &itemRect); /* monsterset 1 */
  MoveTo(itemRect.right + 4, itemRect.top + 12);
  MyrDrawCString("Monster Monsters");
  GetDialogItem(party, 54, &itemType, &itemHandle, &itemRect); /* monsterset 2 (Mega) */
  MoveTo(itemRect.right + 4, itemRect.top + 12);
  MyrDrawCString("Mega Monsters");
update:

  WindowManager_SetEnableRecomposite(0); /* batch; composited once before the loop */

  SetPortDialogPort(party);
  ForeColor(yellowColor);
  RGBBackColor(&greycolor);

  shortupdate(1);

  /* shortupdate(1) -> updateprep() erases the right side of the dialog, which
   * wipes the "To add a character..." instructions (item 89). Redraw the text,
   * then repaint the party members on top of it so the instructions always sit
   * BEHIND the characters. (Drawing the text last would leave it in front of
   * the party, which was especially visible after dropping a character.) */
  gCurrent = party;
  SetPortDialogPort(party);
  {
    int enable_recomposite = WindowManager_SetEnableRecomposite(0);
    ForeColor(yellowColor);
    GetDialogItem(party, 89, &itemType, &itemHandle, &itemRect);
    GetDialogItemText(itemHandle, myString);
    MyrPascalDiStr(89, myString);
    for (t = 0; t <= charnum; t++) {
      GetDialogItem(party, 22 + t, &itemType, &itemHandle, &itemRect);
      itemRect.left -= 10;
      pict(1, itemRect);
      updatechar(t, 5);
    }
    WindowManager_SetEnableRecomposite(enable_recomposite);
  }

shortupdate:

  WindowManager_SetEnableRecomposite(0); /* batch; composited once before the loop */

  gCurrent = party;
  SetPortDialogPort(party);
  TextMode(1);
  TextFont(font);

  GetDialogItem(party, 86, &itemType, &itemHandle, &buttonrect);
  downbutton(FALSE);
  GetDialogItem(party, 87, &itemType, &itemHandle, &buttonrect);
  downbutton(FALSE);

  ForeColor(yellowColor);

  MyrCDiStr(35, (StringPtr) "");
  pict(160, itemRect);
  if (charnum > 4) {
    ploticon3(2019, itemRect);
    charnum = 5;
  }

  GetDialogItem(party, 82 + howhard, &itemType, &itemHandle, &itemRect);
  fit13(&itemRect);
  InsetRect(&itemRect, 2, 2);
  DrawPicture(on, &itemRect);

  monster_btn_rect(party, 55 + monsterset, &itemRect);
  InsetRect(&itemRect, 2, 2);
  DrawPicture(on, &itemRect);

  /* The selected-character indicator (gray bar) only makes sense once the party
   * has at least one member; keep it hidden when the party is empty. */
  if (charnum > -1) {
    GetDialogItem(party, newcharpick + 28, &itemType, &itemHandle, &itemRect);
    DrawPicture(marker, &itemRect);
  }

  totallevel = 0;
  for (t = 0; t <= charnum; t++) {
    DialogNum(61 + t, c[t].level);
    totallevel += c[t].level;
    minus(c[t].name, 1);
  }

  DialogNum(75, totallevel);
  DialogNumLong(74, reclevel);

  /* *** CHANGED FROM ORIGINAL IMPLEMENTATION ***
   * NOTE(jpetrie): This test was changed from 999 to 0 to align with the level restriction change above.
   * (Incorporated from upstream Realmz-Castle/realmz PR #239.)
   */
  if (maxlevel == 0)
    MyrCDiStr(77, (StringPtr) "None");
  else
    DialogNumLong(77, maxlevel);

  total = totallevel;
  required = reclevel;

  hardpercent = 1 + (howhard * .33);

  if (total) {
    percent = (hardpercent * (required / total)) * 100;

    if (percent < 20)
      percent = 20;
    if (percent > 250)
      percent = 250;

    temp = percent;
    DialogNum(72, temp);
  } else
    MyrCDiStr(72, (StringPtr) "");

  percent /= 100;

  /* *** CHANGED FROM ORIGINAL IMPLEMENTATION ***
   * NOTE(fuzziqersoftware): We no longer use the Data CD file to keep track
   * of character files; instead, we enumerate the Character Files directory.
   */
  partyselect_draw_list(index, filepick);
  /* *** END CHANGES *** */

  ForeColor(yellowColor);

  getfilename("Data RI");
  if ((fp = MyrFopen(filename, "rb")) != NULL) {
    fread(&restrictinfo, sizeof restrictinfo, 1, fp);
    CvtRestrictionInfoToPc(&restrictinfo);
    fclose(fp);
    hasrestrictions = TRUE;
    GetDialogItem(party, 21, &itemType, &itemHandle, &itemRect);
    pict(216, itemRect);
  }

  /* Column headers, drawn to line up with their columns:
   *  - "Name"  left-aligned above the scrollable name list (x = 22).
   *  - "Level" above the file list's level column (x = 182).
   *  - "Level" to the upper right of the Add button, above the party's level
   *    column (x = 275); shown only once at least one character is in the party.
   * The header band (above the list frame at y = 46) is cleared first so the
   * party "Level" header is removed cleanly when the last character is dropped. */
  {
    Rect headerrect;
    BackPixPat(base);
    /* Clear the two header regions (clear of the Add button at x>=223 and the
     * list frame at y>=46) so headers redraw cleanly. */
    SetRect(&headerrect, 0, 20, 222, 45);
    EraseRect(&headerrect);
    /* Stop before the selection marker column (x=321) so we don't clip the top
     * of the first added character's selection indicator. */
    SetRect(&headerrect, 270, 0, 321, 20);
    EraseRect(&headerrect);
    TextFont(font);
    TextSize(17);
    TextFace(bold);
    ForeColor(yellowColor);
    MoveTo(22, 38);
    MyrDrawCString("Name");
    MoveTo(177, 38);
    MyrDrawCString("Level");
    if (charnum > -1) {
      MoveTo(275, 15);
      MyrDrawCString("Level");
    }
  }

  /* End of the batched redraw: re-enable compositing, which flushes everything
   * drawn above to the screen in a single composite (no element-by-element
   * flicker). The modal loop below keeps compositing enabled. */
  WindowManager_SetEnableRecomposite(1);

  for (;;) {
  over:
    gCurrent = party;
    FlushEvents(everyEvent, 0);
    ModalDialog(partyselect_filter, &itemHit);

    /* Keyboard navigation (see partyselect_filter). Arrow keys move the
     * highlighted entry in the character-file list; Return/Enter adds the
     * highlighted character, or—when the party is already full—starts the
     * game just like the Done button. */
    if ((itemHit == PS_KEY_DOWN) || (itemHit == PS_KEY_UP)) {
      short count = (short)get_character_list_count();
      short cur = filepick ? (short)(index + filepick - 1) : -1;
      short na;
      const char* nm;
      short lv;
      if (itemHit == PS_KEY_DOWN) {
        for (na = cur + 1; na < count; na++) {
          get_character_info_from_list(na, &nm, &lv);
          if (nm && nm[0])
            break;
        }
        if (na >= count)
          goto over; /* nothing selectable below the current entry */
      } else {
        for (na = (cur < 0 ? count : cur) - 1; na >= 0; na--) {
          get_character_info_from_list(na, &nm, &lv);
          if (nm && nm[0])
            break;
        }
        if (na < 0)
          goto over; /* nothing selectable above the current entry */
      }
      sound(141);
      /* Scroll the list so the newly selected entry is visible, then set
       * filepick to its on-screen row (list rows are items 1..13). */
      if (na < index)
        index = na;
      else if (na > index + 12)
        index = na - 12;
      filepick = na - index + 1;
      /* Only the list changes; redraw it alone (batched) so the party display,
       * marker and instruction text don't flicker. */
      {
        int er = WindowManager_SetEnableRecomposite(0);
        partyselect_draw_list(index, filepick);
        WindowManager_SetEnableRecomposite(er);
      }
      goto over;
    }
    if (itemHit == PS_KEY_ENTER)
      itemHit = (charnum < 5) ? 35 : 34; /* Add, or Done when the party is full */

    GetDialogItem(party, itemHit, &itemType, &itemHandle, &buttonrect);

    if ((itemHit == 21) && (hasrestrictions)) {
      viewrestrictions();
      goto bigupdate;
    }

    if ((itemHit > 79) && (itemHit < 85)) /**** difficulty level *****/
    {

      if ((itemHit > 83) && (!doreg())) {
        SetPortDialogPort(party);
        ForeColor(blackColor);
        BackColor(whiteColor);
        warn(103);
        goto update;
      }

      sound(144);
      GetDialogItem(party, 82 + howhard, &itemType, &itemHandle, &itemRect);
      fit13(&itemRect);
      InsetRect(&itemRect, 2, 2);
      DrawPicture(non, &itemRect);

      howhard = itemHit - 82;
      goto shortupdate;
    }

    if ((itemHit > 53) && (itemHit < 57)) /***** monster set ******/
    {
      if ((itemHit == 56) || (itemHit == 54)) {
        if (!doreg()) {
          SetPortDialogPort(party);
          ForeColor(blackColor);
          BackColor(whiteColor);
          warn(103);
          goto update;
        }
      }

      sound(144);
      monster_btn_rect(party, 55 + monsterset, &itemRect);
      InsetRect(&itemRect, 2, 2);
      DrawPicture(non, &itemRect);

      monsterset = itemHit - 55;

      monster_btn_rect(party, 55 + monsterset, &itemRect);
      InsetRect(&itemRect, 2, 2);
      DrawPicture(on, &itemRect);

      savepref();
    }

    if ((itemHit > 37) && (itemHit < 40)) /***** scroll list up (38) / down (39) one entry *****/
    {
      ploticon3(129, buttonrect);
      sound(141);
      if (itemHit == 39)
        index += 1;
      else
        index -= 1;
      if (index < 0)
        index = 0;
      /* Don't scroll past the end: keep the last entry visible at the bottom. */
      {
        short maxindex = (short)get_character_list_count() - 13;
        if (maxindex < 0)
          maxindex = 0;
        if (index > maxindex)
          index = maxindex;
      }
      ploticon3(130, buttonrect);
      /* Scrolling only changes the list; redraw it alone (batched) so the
       * selection marker and instruction text don't flicker. */
      {
        int er = WindowManager_SetEnableRecomposite(0);
        partyselect_draw_list(index, filepick);
        WindowManager_SetEnableRecomposite(er);
      }
      goto over;
    }

    if ((itemHit == 37) && (filepick)) /****** erase char from disk ******/
    {
      ploticon3(129, buttonrect);
      GetDialogItem(party, filepick, &itemType, &itemHandle, &itemRect);
      GetDialogItemText(itemHandle, myString);
      ploticon3(130, buttonrect);
      if (!StringWidth(myString))
        goto update;

      if (question(3)) {
        GetDialogItem(party, filepick, &itemType, &itemHandle, &itemRect);
        GetDialogItemText(itemHandle, myString);
        PtoCstr(myString);
        minus((Ptr)myString, 0); /****** erase char from disk ******/
        SetPortDialogPort(party);
      }
      filepick = 0;
      goto update;
    }

    if (itemHit == 88) /****** import ******/
    {
/* File loading not ready under carbon yet */
#ifdef CARBON
    /* UGH, GOTOs */
    charerror:
      SetPortDialogPort(party);
      ForeColor(blackColor);
      BackColor(whiteColor);
      warn(38);
      goto bigupdate;

#else

      SFReply SFReplyRecord;
      SFTypeList types = {'APPL', 0, 0, 0};
      char** file_types = {NULL};
      // char *filename = dialog_open_file(file_types, "Please select the character file to import:");
      SFGetFile(where, myString, NIL, -1, types, NIL, &SFReplyRecord);

      if (!SFReplyRecord.good)
        goto over;
      if (filename == NULL)
        goto over;

      GetFInfo(SFReplyRecord.fName, SFReplyRecord.vRefNum, &fileinfo);

      ploticon3(129, buttonrect);

      PtoCstr((StringPtr)gotword);
      PtoCstr(SFReplyRecord.fName);
      strcpy((StringPtr)gotword, SFReplyRecord.fName);

      strcpy(filename, ":Character Files:");
      strncat(filename, gotword, 30);

      ploticon3(130, buttonrect);

      minus((Ptr)SFReplyRecord.fName, 1);

      if ((fp = MyrFopen(filename, "rb")) == NULL) {
      charerror:
        SetPortDialogPort(party);
        ForeColor(blackColor);
        BackColor(whiteColor);
        warn(38);
        goto bigupdate;
      } else {
        fread(&characterl, sizeof characterl, 1, fp);
        CvtCharacterToPc(&characterl);
        fclose(fp);

        if (!checkforerrors())
          plus(characterl.name, characterl.level);
      }
      goto bigupdate;
#endif /* !CARBON */
    }

    if (itemHit == 34) /*********** done button *************/
    {
      ploticon3(129, buttonrect);
      if (charnum > -1)
        sound(26260);
      for (t = 0; t <= charnum; t++)
        plus(c[t].name, c[t].level);

      for (tt = 0; tt <= charnum; tt++) {
        for (t = 0; t < 10; t++) {
          definespells[tt][t][0] = c[tt].definespells[t][0];
          definespells[tt][t][1] = c[tt].definespells[t][1];
          definespells[tt][t][2] = c[tt].definespells[t][2];
          definespells[tt][t][3] = c[tt].definespells[t][3];
        }
      }

      bandaid();
      DisposeDialog(party);
      /* Restore the menus disabled while the party screen was open. */
      EnableItem(gFile, 0);
      EnableItem(gGame, 0);
      EnableItem(gParty, 0);
      DrawMenuBar();
      forcesmall = FALSE;
      return;
    }

    if (itemHit < 14) {
      GetDialogItem(party, itemHit, &itemType, &itemHandle, &itemRect);
      GetDialogItemText(itemHandle, myString);
      PtoCstr(myString);
      /* Clicking an empty row in the character list does nothing — no alert
       * sound and no warning dialog. */
      if (!strlen(myString))
        goto over;
      sound(145);
      strcpy(filename, ":Character Files:");
      strncat(filename, myString, 30);

      if ((fp = MyrFopen(filename, "rb")) == NULL) {
        /* *** CHANGED FROM ORIGINAL IMPLEMENTATION ***
         * NOTE(chromancer): Bug in original code hides selected character
         * when fopen fails, which vanishes the selected on an empty-slot
         * click. Fix: guard on the clicked slot having had a name. warn()
         * overwrites global myString, so capture check before warn().
         */
        short clicked_empty = !strlen(myString);
        SetPortDialogPort(party);
        ForeColor(blackColor);
        BackColor(whiteColor);
        warn(38);
        warn(143);
        if (!clicked_empty) {
          GetDialogItem(party, filepick, &itemType, &itemHandle, &itemRect);
          GetDialogItemText(itemHandle, myString);
          PtoCstr(myString);
          minus((Ptr)myString, 1);
        }
        goto bigupdate;
      }

      fread(&characterl, sizeof characterl, 1, fp);
      CvtCharacterToPc(&characterl);
      fclose(fp);

      if (itemHit == filepick) {
        fp = MyrFopen(filename, "rb");
        characterr = c[0];
        fread(&c[0], sizeof c[0], 1, fp);
        CvtCharacterToPc(&c[0]);

        fclose(fp);
        viewcharacter(0, 1);
        filepick = 0;
        c[0] = characterr;
        goto bigupdate;
      }

      if (!filepick)
        filepick = itemHit;
      ForeColor(cyanColor);
      GetDialogItem(party, filepick, &itemType, &itemHandle, &itemRect);
      GetDialogItemText(itemHandle, myString);
      MyrPascalDiStr(filepick, myString);
      GetDialogItem(party, filepick + 40, &itemType, &itemHandle, &itemRect);
      GetDialogItemText(itemHandle, myString);
      MyrPascalDiStr(filepick + 40, myString);

      ForeColor(yellowColor);
      GetDialogItem(party, itemHit + 40, &itemType, &itemHandle, &itemRect);
      GetDialogItemText(itemHandle, myString);
      MyrPascalDiStr(itemHit + 40, myString);
      GetDialogItem(party, itemHit, &itemType, &itemHandle, &itemRect);
      GetDialogItemText(itemHandle, myString);
      MyrPascalDiStr(itemHit, myString);

      ForeColor(blackColor);
      PtoCstr(myString);
      strcpy(name, myString);
      filepick = itemHit;

      temp = characterl.level;
      SetCCursor(sword);
      if ((temp > restrictinfo.maxlevel) && (restrictinfo.maxlevel)) /******** too high a level character *********/
      {
        SetCCursor(stop);
      }

      if ((charnum + 1 >= restrictinfo.maxpc) && (restrictinfo.maxpc)) /*************** too many characters *******/
      {
        SetCCursor(stop);
      }

      if (restrictinfo.canrace[characterl.race - 1]) /*************** banned race *******/
      {
        SetCCursor(stop);
      }

      if (restrictinfo.cancaste[characterl.caste - 1]) /*************** banned caste *******/
      {
        SetCCursor(stop);
      }
    }

    if ((itemHit > 21) && (itemHit < 23 + charnum)) {
      ploticon3(129, buttonrect);
      sound(141);
      newcharpick = charselectnew = itemHit - 22;
      GetDialogItem(party, oldcharpick + 28, &itemType, &itemHandle, &itemRect);
      DrawPicture(grey, &itemRect);
      GetDialogItem(party, newcharpick + 28, &itemType, &itemHandle, &itemRect);
      DrawPicture(marker, &itemRect);
      if (newcharpick == oldcharpick) {
        ForeColor(yellowColor);
        viewcharacter(newcharpick, 0);
        filepick = 0;
        goto bigupdate;
      }
      oldcharpick = newcharpick;
      ploticon3(130, buttonrect);
    }

    if ((itemHit == 36) && (charnum > -1)) /********** drop from party ****/
    {
      charselectnew = 0;
      if ((mode) && (!charnum)) {
        SetPortDialogPort(party);
        ForeColor(blackColor);
        BackColor(whiteColor);
        warn(67);
        SetPortDialogPort(party);
        ForeColor(yellowColor);
        RGBBackColor(&greycolor);
      } else {
        danger = FALSE;
        for (t = 0; t < c[newcharpick].numitems; t++) {
          loaditem(c[newcharpick].items[t].id);
          if ((item.type == 25) || (item.type == 23) || (item.type < 0))
            danger = TRUE;
        }

        if (danger) {
          SetPortDialogPort(party);
          ForeColor(blackColor);
          BackColor(whiteColor);
          warn(100);
          SetPortDialogPort(party);
          ForeColor(yellowColor);
          RGBBackColor(&greycolor);

          gCurrent = party;
          if (question(13))
            danger = FALSE;
          else
            goto bigupdate;

          GetDialogItem(party, 13, &itemType, &itemHandle, &buttonrect);
        }

        if (!danger) {
          ploticon3(129, buttonrect);
          sound(663);
          plus(c[newcharpick].name, c[newcharpick].level);
          ForeColor(yellowColor);
          savecharacter(newcharpick);

          MyrCDiStr(61 + charnum, (StringPtr) "");

          if ((c[newcharpick].stamina < 1) || (c[newcharpick].condition[COND_ANIMATED]))
            killparty--;

#if CHECK_ILLEGAL_ACCESS > 0
          if (newcharpick < 0 || newcharpick >= 6 || charnum < 0 || charnum >= 6)
            AcamErreur("party select bad index");
#endif
          for (t = newcharpick; t < charnum; t++)
            c[t] = c[t + 1];

          GetDialogItem(party, newcharpick + 22, &itemType, &itemHandle, &itemRect);

          itemRect.top = newcharpick * 50;
          itemRect.bottom = 300;
          itemRect.left -= 10;
          itemRect.right = 640;

          /* Scroll the party rows up to close the gap. The scrolled region spans
           * the full width of the party column (out to x=640), which also covers
           * the "To add a character..." instructions (item 89) that sit BEHIND
           * the party boxes. Compositing this intermediate state would briefly
           * show those instructions jumped up by 50px before bigupdate redraws
           * them. Disable recompositing across the scroll/erase so only
           * bigupdate's final, correct frame is shown (it re-enables compositing
           * just before the modal loop). */
          WindowManager_SetEnableRecomposite(0);
          SetPortDialogPort(party);
          ScrollRect(&itemRect, 0, -50, 0L);
          delay(10); // Fantasoft 7.1  Slows it down a bit on fast machines.

          itemRect.left = 319;
          itemRect.right = 640;
          itemRect.top = 250;
          itemRect.bottom = 300;
          EraseRect(&itemRect);

          charnum--;
          oldcharpick = filepick = 0;
          if (newcharpick > charnum) {
            newcharpick--;
            if (newcharpick < 0)
              newcharpick++;
          }
          ploticon3(130, buttonrect);
          goto bigupdate;
        }
      }
    }

    if ((itemHit == 35) && (charnum < 5) && (filepick)) /**** add to party ****/
    {
      temp = GetDialogNum(filepick + 40);

      /* *** CHANGED FROM ORIGINAL IMPLEMENTATION ***
       * NOTE(jpetrie): Originally, when maxlevel = 999 meant "no level cap," very high level parties could still
       * trigger the "level limit exceeded" error here. Adding an explicit check for the "no level cap" value prevents
       * that from happening.
       * (Incorporated from upstream Realmz-Castle/realmz PR #239.)
       */
      if (maxlevel != 0 && (total + temp > maxlevel) && (!mode)) {
        ForeColor(blackColor);
        BackColor(whiteColor);
        warn(64);
        goto update;
      }

      if (hasrestrictions) {
        GetDialogItem(party, filepick, &itemType, &itemHandle, &itemRect); /******** load in for restiction testing ********/
        GetDialogItemText(itemHandle, myString);
        if (StringWidth(myString)) {
          PtoCstr(myString);
          strcpy(filename, ":Character Files:");
          strcat((StringPtr)filename, myString);
          if ((fp = MyrFopen(filename, "rb")) != NULL) {
            fread(&characterl, sizeof characterl, 1, fp);
            CvtCharacterToPc(&characterl);
            fclose(fp);
          } else
            goto charerror;
        }

        if ((temp > restrictinfo.maxlevel) && (restrictinfo.maxlevel)) /******** too high a level character *********/
        {
          ForeColor(blackColor);
          BackColor(whiteColor);
          warn(134);
          viewrestrictions();
          goto bigupdate;
        }

        if ((charnum + 1 >= restrictinfo.maxpc) && (restrictinfo.maxpc)) /*************** too many characters *******/
        {
          ForeColor(blackColor);
          BackColor(whiteColor);
          warn(135);
          viewrestrictions();
          goto bigupdate;
        }

        if (restrictinfo.canrace[characterl.race - 1]) /*************** banned race *******/
        {
          ForeColor(blackColor);
          BackColor(whiteColor);
          warn(136);
          viewrestrictions();
          goto bigupdate;
        }

        if (restrictinfo.cancaste[characterl.caste - 1]) /*************** banned caste *******/
        {
          ForeColor(blackColor);
          BackColor(whiteColor);
          warn(137);
          viewrestrictions();
          goto bigupdate;
        }
      }

      ploticon3(129, buttonrect);
      GetDialogItem(party, filepick, &itemType, &itemHandle, &itemRect);
      GetDialogItemText(itemHandle, myString);

      if (StringWidth(myString)) {
        PtoCstr(myString);
        sound(662);
        strcpy(filename, ":Character Files:");
        strcat((StringPtr)filename, myString);
        charnum++;
        if ((fp = MyrFopen(filename, "rb")) == NULL) {
          charnum--;
          ForeColor(blackColor);
          BackColor(whiteColor);
          warn(38);
          GetDialogItem(party, filepick, &itemType, &itemHandle, &itemRect);
          GetDialogItemText(itemHandle, myString);
          PtoCstr(myString);
          minus((Ptr)myString, 1);
          goto update;
        }
        fread(&characterl, sizeof characterl, 1, fp);
        CvtCharacterToPc(&characterl);
        fclose(fp);

        checkforerrors(); /**** clean up name and spells data from old version ***/

        if (characterl.version > -3) {
          charnum--;
          ForeColor(blackColor);
          BackColor(whiteColor);
          warn(73);
          goto update;
        }

        if ((characterl.level > 3) && (!doreg())) {
          charnum--;
          ForeColor(blackColor);
          BackColor(whiteColor);
          warn(76);
          goto update;
        }

        c[charnum] = characterl;

        minus(c[charnum].name, 1);
        filepick = 0;
        GetDialogItem(party, 22 + charnum, &itemType, &itemHandle, &itemRect);
        itemRect.left -= 10;
        pict(1, itemRect);
        updatechar(charnum, 5);
        ploticon3(130, buttonrect);

        if ((c[charnum].stamina < 1) || (c[charnum].condition[COND_ANIMATED]))
          killparty++;
        goto shortupdate;
      }
      ploticon3(130, buttonrect);
    }
  }
}

/* Any click anywhere dismisses the View Restrictions screen. */
static Boolean viewrestrictions_filter(DialogPtr dlg, EventRecord* ev, short* hit) {
  (void)dlg;
  if (ev->what == mouseDown) {
    *hit = 1;
    return TRUE;
  }
  return FALSE;
}

/********************************* viewrestrictions ***********************/
void viewrestrictions(void) {
  DialogRef restrict;

  restrict = GetNewDialog(-7931, 0L, (WindowPtr)-1L);
  SetPortDialogPort(restrict);
  gCurrent = restrict;
  showzero = 0;

  /* Center the screen horizontally and vertically within the 800x600 game
   * screen. (The Done/Clear buttons that used to appear here are baked into
   * the background art, PICT -2101 — they are not added programmatically.) */
  {
    Rect pb;
    GetPortBounds((CGrafPtr)GetDialogWindow(restrict), &pb);
    MoveWindow(GetDialogWindow(restrict),
        GlobalLeft + (800 - (pb.right - pb.left)) / 2,
        GlobalTop + (600 - (pb.bottom - pb.top)) / 2, FALSE);
  }

  /* This screen is read-only (any click dismisses it), but its description box
   * is an EDIT_TEXT item that would otherwise grab default focus and draw a
   * stray blinking caret. Clear the focus so no caret is rendered. */
  ClearDialogFocus(restrict);

  DrawDialog(restrict);
  DrawControls(GetDialogWindow(restrict));

  MyrPascalDiStr(4, restrictinfo.description);
  DialogNum(65, restrictinfo.maxpc);
  DialogNum(66, restrictinfo.maxlevel);

  for (t = 0; t < 30; t++) {
    GetDialogItem(gCurrent, t + 5, &itemType, &itemHandle, &itemRect);
    SetControlValue((ControlHandle)itemHandle, restrictinfo.canrace[t]);

    GetDialogItem(gCurrent, t + 35, &itemType, &itemHandle, &itemRect);
    SetControlValue((ControlHandle)itemHandle, restrictinfo.cancaste[t]);

    GetIndString(myString, 129, t + 1); /******** show races ********/
    MyrPascalDiStr(t + 67, myString);

    GetIndString(myString, 131, t + 1); /******** show castes ********/
    MyrPascalDiStr(t + 97, myString);
  }

  BeginUpdate(GetDialogWindow(restrict));
  EndUpdate(GetDialogWindow(restrict));

  FlushEvents(everyEvent, 0);
  ModalDialog(viewrestrictions_filter, &itemHit);

  DisposeDialog(restrict);
}
