#include "prototypes.h"
#include "variables.h"

/************** delay ***********************/
void delay(short timedelay) {
  int32_t oldtick;

  if (!timedelay)
    timedelay = delayspeed;
  oldtick = TickCount();

  for (;;) {
    /* While a combat animation (an attack, spell, or damage display, on either
     * side's turn) plays out, the game spins here rather than in its normal
     * event loop. Service the menu bar each spin so a menu selection takes
     * effect the instant it is made -- including the heavy "End this Adventure"
     * and "Revert to a Previous Game" actions (pass 1). We only do this while
     * combat.c has an abort point armed (inside the battle loop), so a Revert
     * dispatched from here can unwind the combat stack cleanly; a Cancel simply
     * returns and the animation resumes exactly where it paused. */
    if (incombat && RealmzCombatAbortArmed())
      RealmzServiceMenuBar(1);
    if (TickCount() - oldtick > timedelay)
      return;
  }
}
