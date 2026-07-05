#include "prototypes.h"
#include "variables.h"

/************** delay ***********************/
void delay(short timedelay) {
  int32_t oldtick;

  if (!timedelay)
    timedelay = delayspeed;
  oldtick = TickCount();

  for (;;) {
    /* During a computer-controlled combat turn the game spends its time here
     * (and in the monster-move code that calls us) rather than in its normal
     * event loop. Service the menu bar each spin so a menu click pauses the
     * turn immediately and the lightweight Speed/Sound/Music/Preferences items
     * take effect right away. */
    if (incombat && monsterturn)
      RealmzServiceMenuBar(0);
    if (TickCount() - oldtick > timedelay)
      return;
  }
}
