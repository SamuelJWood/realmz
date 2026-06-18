#include "prototypes.h"
#include "variables.h"

/*************************** showque ***************************/
void showque(void) {
  register char t, tt;
  short startx, starty, showloop;
  Rect temprect;
  short spellsize;

  for (showloop = 0; showloop < 60; showloop++) {
    if (que[showloop].duration) {
      spellsize = que[showloop].size - 1;

      /* spellarea is [18][7][7]; a bogus que size would index out of bounds. */
      if (spellsize < 0 || spellsize > 17)
        continue;

      startx = que[showloop].x;
      starty = que[showloop].y;

      temprect.top = (starty - fieldy) * 32 - 32;
      temprect.bottom = temprect.top + 32;

      for (t = 0; t < 7; t++) {
        temprect.top += 32;
        temprect.bottom += 32;
        temprect.left = (startx - fieldx) * 32 - 32;
        temprect.right = temprect.left + 32;
        for (tt = 0; tt < 7; tt++) {
          temprect.left += 32;
          temprect.right += 32;
          if (spellarea[spellsize][t][tt]) {
            short fx = tt + startx;
            short fy = t + starty;
            short cell, statidx;
            /* field is [90][90]; a que placed near a battlefield edge can index
               outside it, yielding a garbage cell value and a wild mapstats[]
               dereference below. Skip out-of-range cells. */
            if (fx < 0 || fx >= 90 || fy < 0 || fy >= 90)
              continue;
            cell = field[fx][fy];
            if (cell > 999) {
              statidx = cell - 1000;
              if (statidx < 0 || statidx >= 402)
                continue;
              if (!mapstats[statidx].solid) {
                fastplot(200 + que[showloop].icon, temprect, 36, 1);
              }
            }
          }
        }
      }
    }
  }
}
