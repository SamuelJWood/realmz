#include "prototypes.h"
#include "variables.h"

/******************************** cansee *******************/
short cansee(short fromx, short fromy, short tox, short toy) {
  short newhit, t;
  Rect orig, dest;
  float deltx, delty, stepx, stepy, partx, party;

  orig.top = fromy * 32;
  orig.left = fromx * 32;

  dest.top = toy * 32;
  dest.left = tox * 32;

  deltx = dest.left - orig.left;
  delty = dest.top - orig.top;

  stepx = deltx / (128 + delayspeed);
  stepy = delty / (128 + delayspeed);

  partx = orig.left;
  party = orig.top;

  for (t = 0; t < 128; t++) {
    newhit = field[fieldx + (orig.left + 16) / 32][fieldy + (orig.top + 16) / 32];

    MyrBitClrShort(&newhit, 1); //**** removes note marker
    MyrBitClrShort(&newhit, 2); //**** removes path marker

    if (newhit > 999) {
      newhit -= 1000;
      if (newhit > 999)
        newhit -= 1000;
      if (newhit > 999)
        newhit -= 1000;

      if (newhit > 0) {
        if (mapstats[newhit].los)
          return (FALSE);
      }
    }
    if (newhit > 399)
      newhit = basetile[lastpix]; // Myriad

    party += stepy;
    partx += stepx;

    orig.top = party;
    orig.left = partx;
  }
  return (TRUE);
}

/******************************** cansee2 *******************/
/* Line-of-sight fog reveal for overhead (non-dungeon) maps.
 *
 * The original implementation cast a small, lopsided "fan" of rays whose shape
 * depended on the party's on-screen column/row (it compared the ray index `t`
 * against the absolute coordinate `curx`/`cury`), so the revealed area was
 * asymmetric and shifted whenever the camera was near a map edge. It has been
 * replaced with recursive shadowcasting: a symmetric, wall-respecting field of
 * view over eight octants that reveals a clean circular radius and correctly
 * stops sight at opaque tiles (mapstats[].los == 1). Wizard Eye still reveals
 * through walls.
 */

#define LOS_RADIUS 7

/* Does the world tile (wx, wy) block line of sight?  Out-of-bounds blocks. The
 * tile-decoding mirrors the rest of the renderer: strip the note/path marker
 * bits, subtract the secret/special 1000s offsets, and clamp overlarge indices
 * to the base tile before consulting mapstats. */
static Boolean los_blocks(short wx, short wy) {
  short hit;

  if (wx < 0 || wx >= 90 || wy < 0 || wy >= 90)
    return TRUE;

  hit = field[wx][wy];
  MyrBitClrShort(&hit, 1); //**** removes note marker
  MyrBitClrShort(&hit, 2); //**** removes path marker

  if (hit > 999) {
    hit -= 1000;
    if (hit > 999)
      hit -= 1000;
    if (hit > 999)
      hit -= 1000;
  }
  if (hit > 399)
    hit = basetile[lastpix]; // Myriad

  if (hit > 0 && hit < 402)
    return (mapstats[hit].los == 1);
  return FALSE;
}

static void los_reveal(short wx, short wy) {
  if (wx >= 0 && wx < 90 && wy >= 0 && wy < 90)
    site[wx][wy] = TRUE;
}

/* Recursive shadowcasting for a single octant, transformed into map space by
 * the (xx, xy, yx, yy) multipliers. `row` is the distance being scanned;
 * `start`/`end` are the slopes bounding the still-visible wedge. */
static void los_cast(short px, short py, short wizard, short row, float start, float end, short xx, short xy,
                     short yx, short yy) {
  short j, dx, dy, mx, my;
  float l_slope, r_slope, new_start = 0.0f;
  Boolean blocked;

  if (start < end)
    return;

  for (j = row; j <= LOS_RADIUS; j++) {
    blocked = FALSE;
    dy = -j;
    for (dx = -j; dx <= 0; dx++) {
      l_slope = (dx - 0.5f) / (dy + 0.5f);
      r_slope = (dx + 0.5f) / (dy - 0.5f);

      if (start < r_slope)
        continue;
      else if (end > l_slope)
        break;

      mx = px + dx * xx + dy * xy;
      my = py + dx * yx + dy * yy;

      /* Reveal cells within the circular radius. */
      if (dx * dx + dy * dy <= LOS_RADIUS * LOS_RADIUS)
        los_reveal(mx, my);

      if (blocked) {
        if (!wizard && los_blocks(mx, my)) {
          new_start = r_slope;
          continue;
        } else {
          blocked = FALSE;
          start = new_start;
        }
      } else if (!wizard && los_blocks(mx, my) && j < LOS_RADIUS) {
        /* An opaque tile mid-scan: recurse for the sub-wedge above it, then
         * keep scanning the row for the shadow it casts. */
        blocked = TRUE;
        los_cast(px, py, wizard, j + 1, start, l_slope, xx, xy, yx, yy);
        new_start = r_slope;
      }
    }
    if (blocked)
      break;
  }
}

/* Octant transform multipliers: {xx, xy, yx, yy} for each of the 8 octants. */
static const short los_mult[4][8] = {
    {1, 0, 0, -1, -1, 0, 0, 1},
    {0, 1, -1, 0, 0, -1, 1, 0},
    {0, 1, 1, 0, 0, -1, -1, 0},
    {1, 0, 0, 1, -1, 0, 0, -1},
};

short cansee2(int32_t fromx, int32_t fromy) {
  short px, py, oct, wizard;

  /* cansee2 is called with on-screen tile coordinates; convert to world tiles
   * via the current camera offset so the reveal never depends on scroll pos. */
  px = lookx + (short)fromx;
  py = looky + (short)fromy;

  wizard = partycondition[PARTY_COND_WIZARD_EYE] ? 1 : 0;

  los_reveal(px, py); /* the party's own tile is always visible */

  for (oct = 0; oct < 8; oct++)
    los_cast(px, py, wizard, 1, 1.0f, 0.0f, los_mult[0][oct], los_mult[1][oct], los_mult[2][oct], los_mult[3][oct]);

  return (NIL);
}
