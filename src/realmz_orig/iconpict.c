#include "prototypes.h"
#include "variables.h"
/******************** iconpicture *****************************/
void iconpicture(short mode) {
  DialogRef portrait;
  short id, lastclick, racenameindex;
  GrafPtr oldport;
  short index = 0;
  short recindex = 0;
  Boolean rollover = 0;

  compactheap();

  id = characterl.iconid;
  GetPort(&oldport);
  if (mode != 2)
    MyrParamText((Ptr) "Select Character Combat Icon", (Ptr) "", (Ptr) "", (Ptr) "");
  portrait = GetNewDialog(170, 0L, (WindowPtr)-1L);
  if (mode == 2)
    MyrCDiStr(70, (StringPtr) "");
  gCurrent = portrait;
  SetPortDialogPort(portrait);
  BackPixPat(base);
  TextFont(font);
  TextFace(bold);
  TextSize(16);
  ForeColor(yellowColor);

  needdungeonupdate = TRUE;

  MoveWindow(GetDialogWindow(portrait), GlobalLeft, GlobalTop, FALSE);
  ShowWindow(GetDialogWindow(portrait));
  ErasePortRect();

  if (mode == 2)
    MyrCDiStr(70, (StringPtr) "");

  DrawDialog(portrait);

  if (mode != 2) {
    GetDialogItem(gCurrent, 70, &itemType, &itemHandle, &buttonrect);
    downbutton(FALSE);
    GetDialogItem(gCurrent, 72, &itemType, &itemHandle, &itemRect);
    pict(213, itemRect);
  }

updaterecomended:

  GetDialogItem(gCurrent, 69, &itemType, &itemHandle, &buttonrect);
  downbutton(FALSE);

  if (mode != 2) /**** dont show recomended for party icon ****/
  {
    recindex = (characterl.race * 6) - 6;
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
      iconhand = GetCIcon(t + 8997 + recindex);
      EraseRect(&buttonrect);
      upbutton(FALSE);
      if (iconhand != NIL) {
        if ((**iconhand).iconBMap.bounds.bottom == 32)
          InsetRect(&buttonrect, 6, 6);
        PlotCIcon(&buttonrect, iconhand);
        if ((**iconhand).iconBMap.bounds.bottom == 32)
          InsetRect(&buttonrect, -6, -6);
        DisposeCIcon(iconhand);
      }
    }
  }

  gStop = FALSE;

  if (!mode) {
    for (t = 0; t <= charnum; t++) {
      GetDialogItem(portrait, 51 + t, &itemType, &itemHandle, &itemRect);
      plotportrait(c[t].pictid, itemRect, c[t].caste, -1);

      GetDialogItem(portrait, 45 + t, &itemType, &itemHandle, &itemRect);
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
  } else if (mode != 2) {
    GetDialogItem(portrait, 33, &itemType, &itemHandle, &itemRect);
    pict(154, itemRect);

    GetDialogItem(portrait, 51, &itemType, &itemHandle, &itemRect);
    plotportrait(characterl.pictid, itemRect, characterl.caste, -1);

    GetDialogItem(portrait, 45, &itemType, &itemHandle, &itemRect);
    ploticon3(characterl.iconid, itemRect);

    CtoPstr(characterl.name);
    ForeColor(yellowColor);
    MyrPascalDiStr(63, (StringPtr)characterl.name);
    PtoCstr((StringPtr)characterl.name);
    GetDialogItem(portrait, 57, &itemType, &itemHandle, &buttonrect);
    downbutton(FALSE);
  } else /********** pick new indoor icon *********/
  {
    GetDialogItem(portrait, 45, &itemType, &itemHandle, &itemRect);
    ploticon3(9000 + horseicon, itemRect);
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
    iconhand = GetCIcon(t + 8991 + index);
    EraseRect(&buttonrect);
    upbutton(FALSE);
    if (iconhand) {
      if ((**iconhand).iconBMap.bounds.bottom == 32)
        InsetRect(&buttonrect, 6, 6);
      PlotCIcon(&buttonrect, iconhand);
      if ((**iconhand).iconBMap.bounds.bottom == 32)
        InsetRect(&buttonrect, -6, -6);
      DisposeCIcon(iconhand);
    } else
      rollover = TRUE;
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

    if ((itemHit > 2) && (itemHit < 33)) /*************** pick tactical **********/
    {
      if ((mode == 2) && (itemHit > 8)) /*********** choose party icon ***/
      {
        sound(130);
        horseicon = itemHit - 9 + index;

        GetDialogItem(portrait, 45, &itemType, &itemHandle, &itemRect);
        EraseRect(&itemRect);
        ploticon3(9000 + horseicon, itemRect);
      } else if (mode != 2) {
        sound(130);
        if (itemHit < 9)
          characterl.iconid = itemHit + recindex + 8997;
        else
          characterl.iconid = itemHit + index + 8991;
        GetDialogItem(portrait, charselectnew + 33, &itemType, &itemHandle, &itemRect);
        pict(154, itemRect);
        GetDialogItem(portrait, charselectnew + 45, &itemType, &itemHandle, &itemRect);
        EraseRect(&itemRect);
        ploticon3(characterl.iconid, itemRect);
        lastclick = itemHit;
      }
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

      GetDialogItem(portrait, charselectnew + 33, &itemType, &itemHandle, &itemRect);
      ploticon3(130, itemRect);

      GetDialogItem(portrait, itemHit, &itemType, &itemHandle, &itemRect);
      ploticon3(129, itemRect);

      c[charselectnew] = characterl;
      charselectnew = itemHit - 33;
      characterl = c[charselectnew];

      goto updaterecomended;
    }
  }
  sound(141);
  if (mode != 2)
    c[charselectnew] = characterl;
  DisposeDialog(portrait);
  SetPort(oldport);
}
