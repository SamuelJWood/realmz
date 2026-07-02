#include "prototypes.h"
#include "variables.h"

/**************************************** checklayout **************************/
short checklayout(int32_t currentlevel) {
  short checkforlevel = -1;
  short newlevel = 0;
  short levelx, levely;
  FILE* fp;

  if (currentlevel)
    checkforlevel = currentlevel;

  getfilename("Layout");
  if ((fp = MyrFopen(filename, "rb")) == NULL)
    return (-2); /***** dont use layout **********/
  fread(&layout, sizeof layout, 1, fp);
  CvtLayoutToPc((int16_t*)layout);
  fclose(fp);

  for (levely = 0; levely < 8; levely++) {
    for (levelx = 0; levelx < 16; levelx++) {
      if (layout[levely][levelx] == checkforlevel)
        goto gotcurrent;
    }
  }

gotcurrent:

  /* Step to the neighbouring layout cell in the direction of travel. The
   * original keyed off the party's on-screen column/row being pinned at the
   * viewport edge; now that a transition can fire as the party steps *onto* the
   * boundary tile (not only when stepping past it), the party is not yet pinned
   * to the extreme column/row, so decide purely from the movement delta. */
  if (deltax > 0)
    levelx++;
  else if (deltax < 0)
    levelx--;

  if (deltay > 0)
    levely++;
  else if (deltay < 0)
    levely--;

  if ((levelx < 0) || (levely < 0) || (levelx > 15) || (levely > 7))
    return (-2); /**************** out of bounds **************/

  if ((!layout[levely][levelx]) || (layout[levely][levelx] == checkforlevel))
    return (-2);
  else {
    if (layout[levely][levelx] == -1)
      return (0);
    else
      return (layout[levely][levelx]);
  }
}

/***************************** tryedgeslide ***********************
 * Handle walking between adjacent overworld regions at a map boundary.
 *
 * Historically a region change only fired when the party tried to step *past*
 * the edge of the map (destination world tile < 0 or > 89). Reaching that point
 * required the party to already be standing on the outermost tile, which is
 * pinned to the very edge of the viewport. With the mouse there is no tile to
 * click beyond the party there, so crossing was keyboard-only.
 *
 * This fires the transition as the party steps *onto* the boundary tile (the
 * common case for both mouse and keyboard) as well as past it (keyboard from
 * the edge). To avoid ever leaving the party pinned on the un-clickable edge
 * tile of the destination region (which would make the return trip impossible
 * for the same reason), the party is placed one tile in from the boundary it
 * arrives at. The outermost ring of a region is uniform border terrain, so no
 * reachable content is skipped.
 *
 * Returns TRUE if a region change occurred (in which case deltax/deltay are
 * cleared and the view has been recentred on the new region).
 */
Boolean tryedgeslide(void) {
  short worldx, worldy, destx, desty, hit;
  short newlandlevel;
  Boolean beyond, onedge;

  if (indung || incombat)
    return (FALSE);

  worldx = partyx + lookx;
  worldy = partyy + looky;
  destx = worldx + deltax;
  desty = worldy + deltay;

  beyond = (destx < 0) || (destx > 89) || (desty < 0) || (desty > 89);
  onedge = ((deltax > 0) && (destx == 89)) || ((deltax < 0) && (destx == 0)) ||
           ((deltay > 0) && (desty == 89)) || ((deltay < 0) && (desty == 0));

  if ((!beyond) && (!onedge))
    return (FALSE);

  /* When merely stepping *onto* an in-bounds boundary tile, defer to normal tile
   * handling if that tile is a door/secret (id > 999) so edge-placed doors still
   * work; only plain border terrain triggers a region slide. */
  if (onedge && (!beyond)) {
    hit = field[destx][desty];
    MyrBitClrShort(&hit, 1); /**** removes note marker ****/
    MyrBitClrShort(&hit, 2); /**** removes path marker ****/
    if ((hit > 2999) || (hit < -2999)) {
      if (hit > 2999)
        hit -= 3000;
      else
        hit += 3000;
    }
    hit = abs(hit);
    if (hit > 999)
      return (FALSE);
  }

  newlandlevel = checklayout(landlevel);
  if (newlandlevel == -2) {
    /* No adjacent region. Block a step that would leave the map entirely, but
     * still allow the party to stand on the outermost tile (there is nowhere to
     * slide to, so it is not a mere border in this direction). */
    if (beyond)
      deltax = deltay = 0;
    return (FALSE);
  }

  saveland(landlevel);
  landlevel = newlandlevel;

  /* Enter the new region one tile in from the boundary we crossed, preserving
   * position along the other axis. */
  lookx = 0;
  looky = 0;
  if (deltax > 0)
    partyx = 1;
  else if (deltax < 0)
    partyx = 88;
  else
    partyx = worldx;
  if (deltay > 0)
    partyy = 1;
  else if (deltay < 0)
    partyy = 88;
  else
    partyy = worldy;

  loadland(landlevel, 1);
  centerpict();
  deltax = deltay = 0;
  return (TRUE);
}

/***************************** checkkeypad *************************/
short checkkeypad(short mode) {
  char maskKey;

  deltax = deltay = whichset = fastspell = 0;
  switch (scanCode) /*** use ***/
  {
    case 0x12: /**** spell 1 *** '1' */;
      whichset = 1;
      break;

    case 0x13: /**** spell 2 *** '2' */;
      whichset = 2;
      break;

    case 0x14: /**** spell 3 *** '3' */;
      whichset = 3;
      break;

    case 0x15: /**** spell 4 *** '4' */;
      whichset = 4;
      break;

    case 0x17: /**** spell 5 *** '5' */;
      whichset = 5;
      break;

    case 0x16: /**** spell 6 *** '6' */;
      whichset = 6;
      break;

    case 0x1a: /**** spell 7 *** '7' */;
      whichset = 7;
      break;

    case 0x1c: /**** spell 8 *** '8' */;
      whichset = 8;
      break;

    case 0x19: /**** spell 9 *** '9' */;
      whichset = 9;
      break;

    case 0x1d: /**** spell 10 *** '0' */;
      whichset = 10;
      break;

    case 0x7E: /*** Up Arrow ***/
    case 0x5b: /*** '8' on keypayd ***/
      deltay = -1;
      break;

    case 0x7D: /*** Down Arrow ***/
    case 0x54: /*** '2' on keypayd ***/
      deltay = 1;
      break;

    case 0x7b: /*** Left Arrow ***/
    case 0x56: /*** '4' on keypayd ***/
      deltax = -1;
      break;

    case 0x7c: /*** Right Arrow ***/
    case 0x58: /*** '6' on keypayd ***/
      deltax = 1;
      break;

    case 0x59: /*** '7' on keypayd ***/
      deltax = -1;
      deltay = -1;
      break;

    case 0x5c: /*** '9' on keypayd ***/
      deltax = 1;
      deltay = -1;
      break;

    case 0x53: /*** '1' on keypayd ***/
      deltax = -1;
      deltay = 1;
      break;

    case 0x55: /*** '3' on keypayd ***/
      deltax = 1;
      deltay = 1;
      break;

    case 0x24: /*** return ***/
      point.v = 51 * charselectnew;
      theControl = charmainbut;
      key = 0;
      break;
  }

  if (mode) {
    if (!incombat) {
      if (whichset) /********* fast spells *********/
      {
        if ((!incombat) || ((incombat) && (q[up] < 6))) {
          castcaste = definespells[charselectnew][whichset - 1][0];
          castlevel = definespells[charselectnew][whichset - 1][1];
          castnum = definespells[charselectnew][whichset - 1][2];
          powerlevel = definespells[charselectnew][whichset - 1][3];
          loadspell(castcaste, castlevel, castnum);
          memoryspell = TRUE;
        }

        if (incombat)
          c[charup].spellscast++;

        if (gTheEvent.modifiers & cmdKey) /*** use ***/
        {
          if (abs(spellinfo.cost * powerlevel) >= c[charselectnew].spellpoints) {
            SetPort(GetWindowPort(screen));
            BackPixPat(base);
            inbooty = TRUE;
            textbox(3, 1, 0, 0, textrect);
            inbooty = FALSE;
            TextMode(1);
            TextSize(20);
            ForeColor(yellowColor);
            MoveTo(30, 350);
            MyrDrawCString("Not Enough Spell Points.");
            sound(-143);
          } else {

            if ((!spellinfo.incamp) || (!powerlevel))
              sound(143);
            else {
              c[charselectnew].spellpoints -= abs(spellinfo.cost * powerlevel);
              lastspell[charselectnew][0] = castlevel;
              lastspell[charselectnew][1] = castnum;
              oldpowerlevel[charselectnew] = powerlevel;
              charselected[charselectnew] = fastspell = TRUE;
              theControl = castspellsbut;
            }
          }
        } else /***** display name only *********/
        {
          SetPort(GetWindowPort(screen));
          inbooty = TRUE;
          textbox(3, 1, 0, 0, textrect);
          inbooty = FALSE;
          TextSize(20);
          TextMode(1);
          BackPixPat(base);
          ForeColor(yellowColor);
          MoveTo(30, 350);
          GetIndString(myString, 1000 * (castcaste + 1) + castlevel, castnum + 1);
          if (powerlevel) {
            PtoCstr(myString);
            MyrDrawCString((Ptr)myString);
            MyrDrawCString("  X ");
            MyrNumToString(powerlevel, myString);
            MyrDrawCString((Ptr)myString);
          } else {
            MoveTo(30, 350);
            MyrDrawCString("Undefined Spell");
            MoveTo(30, 350);
            sound(-143);
            inbooty = TRUE;
            textbox(3, 1, 0, 0, textrect);
            inbooty = FALSE;
          }
        }
      }
    }
  } else {
    if (!incombat) {
      maskKey = BitAnd(key, charCodeMask);
      switch (maskKey) {

        case 'l': /***** L   scroll    *****/
          if ((!inspell) && (checkfortype(charselectnew, 13, TRUE)))
            theControl = viewspellsbut;
          break;

        case 'k': /***** k   create scroll    *****/
          if ((!inspell) && (checkfortype(charselectnew, 13, TRUE))) {
            if ((incamp) && (c[charselectnew].stamina > 0))
              theControl = overviewbut;
          }
          break;

        case 'c': /***** c   Camp    *****/
          theControl = campbut;
          break;

        case 'i': /***** i   items    *****/
          theControl = itemsbut;
          break;

        case 'm': /***** m   money    *****/
          theControl = swapbut;
          break;

        case 's': /***** s   spells    *****/
          theControl = castspellsbut;
          break;

        case 't': /***** t   trade    *****/
          if (charnum)
            theControl = tradebut;
          break;

        case 'h': /***** h   heal    *****/
          theControl = barbut;
          break;

        case 'r': /***** r   rest    *****/
          if (incamp == TRUE)
            theControl = rest;
          break;

        case 'e': /***** e   encounter    *****/
          if ((!incamp) && (!shopavail) && (!templeavail))
            theControl = shopbut;
          break;

        case 'a': /***** a   area search    *****/
          if (!incamp)
            theControl = overviewbut;
          break;

        case 'g': /***** g   go shop / Temple   *****/
          if ((!incamp) && ((shopavail) || (templeavail)))
            theControl = shopbut;
          break;
      }
    }

    if (!incombat)
      tryedgeslide();

    if ((deltax) || (deltay))
      theControl = movelook;
  }
  return (fastspell);
}
