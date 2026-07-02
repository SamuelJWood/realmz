#include "prototypes.h"
#include "variables.h"

/************************ resist ***************************/
short resist(short who) {
  short ttt, chance;
  reply = FALSE;

  if (who < 0)
    return (NIL);

  if (((spellinfo.damagetype < 0) && (abs(spellinfo.damagetype) != 9)) || (spellinfo.spellclass == 0)) /*** DRVs. caster **********/
  {
    chance = 35;

    if (spellinfo.spellclass == 0) /********* charming ******/
    {
      if (who < 9)
        chance = c[who].save[0];
      else {
        chance = 35 + 4 * monster[who - 10].hd;
        if (monster[who - 10].type[0])
          chance += 5; /*** magic using ***/
        if (monster[who - 10].type[5])
          chance += 5; /*** intelligent ***/
      }
    } else {
      if (who < 9)
        chance += 5 * c[who].level;
      else
        chance += 5 * monster[who - 10].hd;

      if (q[up] < 9)
        chance -= 5 * c[charup].level;
      else
        chance -= 5 * monster[monsterup].hd;
    }

    if (spellinfo.saveadjust)
      chance += (powerlevel * spellinfo.saveadjust);

    if (Rand(100) <= chance) {
      showresults(who, -25, 0);
      delay(0);
      return (2);
    }
  }

  /* *** CHANGED FROM ORIGINAL IMPLEMENTATION ***
   * spellimmune has six entries (classes 0-5). The original "< 7" guard let a
   * class 6 spell (e.g. Cosmic Blast) index spellimmune[6], one element past the
   * array. The field that follows in memory is money[0], so any monster carrying
   * gold read as "immune" and always resisted. resolvespell.c already guards the
   * same spellimmune lookup with "< 6"; match it here. */
  if ((who > 9) && (spellinfo.spellclass < 6)) {
    if ((monster[who - 10].spellimmune[spellinfo.spellclass]) || (monster[who - 10].magres > 100))
      return (TRUE);
  }
  /* *** END CHANGES *** */

  if (((spellinfo.cannot == 1) || (spellinfo.cannot > 2)) && (spellinfo.spellclass != 9) && (spellinfo.spellclass != -9))
    return (FALSE);

  if (who < 9) {
    for (ttt = castlevel; ttt < 5; ttt++)
      if (c[who].condition[ttt + 16])
        reply = TRUE; /*********** magic screen *********/
    /************* annimated immune to charm and mental spells **********/
    if (((spellinfo.spellclass == 0) || (spellinfo.spellclass == 5)) && (c[who].condition[COND_ANIMATED]))
      return (TRUE);
    if ((abs(spellinfo.spellclass) == 9) && (inspell)) {
      reply = FALSE;
      if (c[who].condition[COND_SHIELD_FROM_PROJECTILES])
        return (TRUE); /**** missle shield ****/
      if (Rand(100) <= (c[who].dodge - (spellinfo.tohitbonus)))
        return (TRUE); /**** Missile dodge ****/
    } else if (Rand(100) <= c[who].magres + (powerlevel * spellinfo.resistadjust))
    /* *** CHANGED FROM ORIGINAL IMPLEMENTATION ***
    * NOTE(chromancer): Original code inherited had return (FALSE), meaning magres never did anything,
    * except overriding the 'magic screen' (Protection from Nth Level) conditions, causing PCs to get
    * hit when they would otherwise be immune. Note monster magres still worked.
    * (Incorporated from upstream Realmz-Castle/realmz PR #231.)
    */
      return (TRUE);
  } else {
    for (ttt = castlevel; ttt < 5; ttt++)
      if (monster[who - 10].condition[ttt + 16])
        reply = TRUE; /*********** magic screen *********/
    /************* annimated immune to charm and mental spells **********/
    if (((spellinfo.spellclass == 0) || (spellinfo.spellclass == 5)) && (monster[who - 10].condition[COND_ANIMATED]))
      return (TRUE);
    if ((abs(spellinfo.spellclass) == 9) && (inspell)) {
      reply = FALSE;
      if (monster[who - 10].condition[COND_SHIELD_FROM_PROJECTILES])
        return (TRUE); /***** missle shield ***/
      chance = 10 + 5 * monster[who - 10].dx - c[charup].missile - spellinfo.tohitbonus;
      if (Rand(100) <= chance)
        return (TRUE); /**** missile dodge ****/
    } else if (Rand(100) <= monster[who - 10].magres + (powerlevel * spellinfo.resistadjust))
      reply = TRUE;
  }
  return (reply);
}
