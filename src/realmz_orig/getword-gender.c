#include "prototypes.h"
#include "variables.h"

/******************************** getword ******************************/
void getword(void) {
  DialogRef win;
  CCrsrHandle edit;

  edit = GetCCursor(155);
  SetCCursor(edit);
  DisposeCCursor(edit);

  win = GetNewDialog(157, 0L, (WindowPtr)-1L);
  SetPortDialogPort(win);
  BackPixPat(base);
  TextFont(defaultfont);
  TextSize(18);
  ForeColor(yellowColor);

  MoveWindow(GetDialogWindow(win), GlobalLeft + 30, GlobalTop + 110, FALSE);
  ShowWindow(GetDialogWindow(win));
  ErasePortRect();

  for (;;) {
    FlushEvents(everyEvent, 0);
    ModalDialog(0L, &itemHit);

    if (itemHit == 4) {
      GetDialogItem(win, 4, &itemType, &itemHandle, &buttonrect);
      ploticon3(129, buttonrect);
      GetDialogItem(win, 2, &itemType, &itemHandle, &itemRect);
      GetDialogItemText(itemHandle, (StringPtr)gotword);
      DisposeDialog(win);
      SetCCursor(sword);
      return;
    }
  }
}

static Boolean Gender_filter(DialogPtr dlg, EventRecord* ev, short* hit) {
  (void)dlg;
  if (ev->what == keyDown) {
    uint8_t vkey = (uint8_t)((ev->message >> 8) & 0xFF);
    if (vkey == 0x7B) { *hit = 2; return TRUE; } /* Left arrow  → Male   */
    if (vkey == 0x7C) { *hit = 3; return TRUE; } /* Right arrow → Female */
  }
  return FALSE;
}

/***************************** Gender ********************************/
void Gender(void) {
  DialogRef gGeneration;
  short oldhit = 2;

  gGeneration = GetNewDialog(133, 0L, (WindowPtr)-1L);
  SetPortDialogPort(gGeneration);
  BackPixPat(base);
  TextSize(22);
  ForeColor(yellowColor);
  TextFont(font);
  gStop = FALSE;

  {
    Rect pb;
    GetPortBounds((CGrafPtr)GetDialogWindow(gGeneration), &pb);
    short dlgW = pb.right - pb.left;
    short dlgH = pb.bottom - pb.top;
    MoveWindow(GetDialogWindow(gGeneration), (800 - dlgW) / 2, (600 - dlgH) / 2, FALSE);
  }
  ShowWindow(GetDialogWindow(gGeneration));
  ErasePortRect();

  GetDialogItem(gGeneration, 2, &itemType, &itemHandle, &itemRect);
  SetDialogItemText(itemHandle, (StringPtr) "");

  DrawPicture(gen, &itemRect);
  MoveTo(itemRect.left + 30, itemRect.top + 25);
  MyrDrawCString("Male");
  GetDialogItem(gGeneration, 3, &itemType, &itemHandle, &itemRect);
  SetDialogItemText(itemHandle, (StringPtr) "");
  DrawPicture(gen, &itemRect);
  MoveTo(itemRect.left + 30, itemRect.top + 25);
  MyrDrawCString("Female");

  while (gStop == FALSE) {
    FlushEvents(everyEvent, 0);
    ModalDialog(Gender_filter, &itemHit);

    if ((itemHit != 6) && (itemHit != 1)) {
      GetDialogItem(gGeneration, itemHit + 5, &itemType, &itemHandle, &buttonrect);
      downbutton(FALSE);
      sound(130);
      GetDialogItem(gGeneration, oldhit + 2, &itemType, &itemHandle, &itemRect);
      DrawPicture(non, &itemRect);
      characterl.gender = itemHit - 1;
      GetDialogItem(gGeneration, itemHit + 2, &itemType, &itemHandle, &itemRect);
      DrawPicture(on, &itemRect);
      oldhit = itemHit;
      upbutton(FALSE);
    }

    else if (((itemHit == 6) || (itemHit == 1)) && (characterl.gender != 0)) {
      GetDialogItem(gGeneration, 6, &itemType, &itemHandle, &buttonrect);
      ploticon3(133, buttonrect);
      sound(6001);
      gStop = TRUE;
    } else if ((itemHit == 6) || (itemHit == 1))
      sound(6000);
  }

  DisposeDialog(gGeneration);
}
