#include "prototypes.h"
#include "variables.h"

/************** delay ***********************/
void delay(short timedelay) {
  int32_t oldtick;

  if (!timedelay)
    timedelay = delayspeed;
  oldtick = TickCount();

  for (;;)
    if (TickCount() - oldtick > timedelay)
      return;
}
