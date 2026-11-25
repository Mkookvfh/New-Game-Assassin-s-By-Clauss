/* THE ASHEN KNIGHT — fighters and townsfolk.
 *
 * One parametric builder draws the player, the enemy knights, the warden and
 * the villagers.  Local space: forward is -y, up is +z, right is +x; the root
 * frame is spun by the actor yaw.
 *
 * The models are box-built like a 2005 PS2 game, but with layered armour:
 * cuisse over mail, poleyn, greave, boot, two-plate pauldron, gorget, domed
 * helm with a visor and a crest.  That layering (and the seams between the
 * pieces) is what makes a low-poly figure read as a knight instead of a box.
 */
#include <math.h>
#include "game.h"

typedef struct {
  int ar, ag, ab;       /* plate steel */
  int cr, cg, cb;       /* cloth / cape */
  int tr, tg, tb;       /* trim, brass */
  int lr, lg, lb;       /* leather / mail */
} Palette;

static const Palette pal_player = { 156, 164, 178,  128, 26, 30,  196, 162, 92,  44, 42, 46 };
static const Palette pal_enemy  = { 138, 130, 122,   72, 26, 30,  148, 120, 74,  38, 34, 32 };
static const Palette pal_boss   = { 112, 106, 126,   58, 24, 36,  198, 166, 92,  34, 30, 36 };
static const Palette pal_vill_a = {  92,  86,  78,  140, 120,  92,  150, 128,  88,  62, 52, 40 };
static const Palette pal_vill_b = {  84,  88,  80,  106, 116,  96,  150, 128,  88,  54, 50, 42 };
static const Palette pal_vill_c = {  88,  82,  88,  116, 104, 122,  150, 128,  88,  58, 50, 54 };

static int flashmix = 0;

static float dist2(float ax, float ay, float bx, float by)
{
  float dx = ax - bx, dy = ay - by;
  return dx * dx + dy * dy;
}

static void setc(int r, int g, int b)
{
  if (flashmix) {
    r += (255 - r) * 3 / 5;
    g += (255 - g) * 3 / 5;
    b += (255 - b) * 3 / 5;
  }
  mesh_tint(r, g, b);
}

static void setp(const Palette *p) { setc(p->ar, p->ag, p->ab); }
static void setl(const Palette *p) { setc(p->lr, p->lg, p->lb); }
static void sett(const Palette *p) { setc(p->tr, p->tg, p->tb); }
static void setcl(const Palette *p) { setc(p->cr, p->cg, p->cb); }

/* a plate or a limb: half extents in, tint already set */
static void bx(Xf f, int mat, float cx, float cy, float cz,
               float hx, float hy, float hz, int caps)
{
  xf_box(f, mat, cx, cy, cz, hx, hy, hz, caps);
}

/* ----------------------------------------------------------------- weapons */

/* sword hanging from the hand frame sf: grip, guard, tapered blade */
static void sword(Xf sf, const Palette *p, float sc)
{
  sett(p);
  bx(sf, MAT_GOLD, 0.0f, 0.0f, 8.5f * sc, 1.7f * sc, 1.7f * sc, 4.0f * sc, 0);   /* pommel */
  setl(p);
  bx(sf, MAT_DARK, 0.0f, 0.0f, 4.0f * sc, 1.4f * sc, 1.4f * sc, 4.0f * sc, 0);   /* grip */
  sett(p);
  bx(sf, MAT_GOLD, 0.0f, 0.0f, 0.0f, 9.5f * sc, 1.9f * sc, 1.8f * sc, 1);        /* guard */

  setc(206, 214, 232);
  bx(sf, MAT_IRON, 0.0f, 0.0f, -24.0f * sc, 2.7f * sc, 1.1f * sc, 21.0f * sc, 0); /* blade */
  bx(sf, MAT_IRON, 0.0f, 0.0f, -47.0f * sc, 2.1f * sc, 0.9f * sc, 3.5f * sc, 0);
  setc(228, 234, 250);
  bx(sf, MAT_IRON, 0.0f, 0.0f, -52.0f * sc, 1.3f * sc, 0.6f * sc, 2.6f * sc, 0); /* point */
  /* fuller: a bright line down the middle of the blade */
  setc(240, 246, 255);
  mq(sf, MAT_IRON, -0.5f * sc, -1.2f * sc, -6.0f * sc,
     0.5f * sc, -1.2f * sc, -6.0f * sc,
     0.5f * sc, -1.2f * sc, -42.0f * sc,
     -0.5f * sc, -1.2f * sc, -42.0f * sc);
}

/* round shield on the forearm */
static void shield(Xf af, const Palette *p, float sc)
{
  setcl(p);
  bx(af, MAT_PLANK, -2.0f * sc, -7.0f * sc, -26.0f * sc,
     11.0f * sc, 2.2f * sc, 13.0f * sc, 0);
  setl(p);
  bx(af, MAT_DARK, -2.0f * sc, -9.0f * sc, -26.0f * sc,
     9.0f * sc, 1.0f * sc, 11.0f * sc, 0);
  sett(p);
  bx(af, MAT_GOLD, -2.0f * sc, -10.2f * sc, -26.0f * sc,
     3.2f * sc, 1.0f * sc, 3.2f * sc, 1);
}

/* --------------------------------------------------------------- body parts */

static void leg(Xf base, float sx, float swing, const Palette *p, float sc, int far)
{
  Xf lf = xf_move(base, sx, 0.0f, 48.0f * sc);

  lf = xf_pivot_rotx(lf, swing, 0.0f, 0.0f);

  /* thigh: padded mail with a steel cuisse strapped over the front */
  setl(p);
  bx(lf, MAT_DARK, 0.0f, 0.0f, -13.0f * sc, 5.4f * sc, 6.0f * sc, 11.0f * sc, 0);
  setp(p);
  bx(lf, MAT_IRON, 0.0f, -1.2f * sc, -12.0f * sc, 5.5f * sc, 5.6f * sc, 9.5f * sc, 0);
  /* knee cop */
  sett(p);
  bx(lf, MAT_IRON, 0.0f, -1.4f * sc, -25.0f * sc, 5.8f * sc, 6.2f * sc, 3.2f * sc, 1);
  /* greave over the shin */
  setp(p);
  bx(lf, MAT_IRON, 0.0f, 0.0f, -35.0f * sc, 4.9f * sc, 5.2f * sc, 8.5f * sc, 0);
  /* boot */
  setl(p);
  bx(lf, MAT_DARK, 0.0f, -2.2f * sc, -45.5f * sc, 5.4f * sc, 7.8f * sc, 3.6f * sc, 1);
  if (!far) {
    sett(p);
    bx(lf, MAT_GOLD, 0.0f, -2.2f * sc, -43.0f * sc, 3.4f * sc, 4.0f * sc, 1.0f * sc, 0);
  }
}

/* arm frame: pivot at the shoulder so it swings from the right place */
static Xf arm_frame(Xf base, float sx, float a, float sc)
{
  Xf af = xf_move(base, sx, 0.0f, 74.0f * sc);
  return xf_pivot_rotx(af, a, 0.0f, 0.0f);
}

static void arm(Xf af, const Palette *p, float sc, int far)
{
  setp(p);
  bx(af, MAT_IRON, 0.0f, 0.0f, -3.0f * sc, 4.4f * sc, 4.8f * sc, 6.0f * sc, 0);   /* upper */
  sett(p);
  bx(af, MAT_IRON, 0.0f, -0.4f * sc, -11.0f * sc, 4.6f * sc, 5.0f * sc, 3.0f * sc, 1); /* couter */
  setp(p);
  bx(af, MAT_IRON, 0.0f, 0.0f, -20.0f * sc, 4.0f * sc, 4.4f * sc, 7.0f * sc, 0);   /* forearm */
  setl(p);
  bx(af, MAT_DARK, 0.0f, -0.4f * sc, -29.0f * sc, 4.2f * sc, 4.8f * sc, 4.0f * sc, 1); /* gauntlet */
  if (!far) {
    sett(p);
    bx(af, MAT_GOLD, 0.0f, -3.6f * sc, -30.0f * sc, 3.0f * sc, 2.0f * sc, 3.0f * sc, 0);
  }
}

/* pauldron: two overlapping plates, the PS2 way of faking a rounded cop */
static void pauldron(Xf base, float sx, const Palette *p, float sc)
{
  float in = sx < 0.0f ? -1.0f : 1.0f;   /* sign: which way the shoulder faces */
  float ax = sx - in * 5.6f * sc, ay = 7.1f * sc, az = 83.0f * sc;
  float bx_ = sx + in * 4.4f * sc, by = 6.4f * sc, bz = 80.2f * sc;
  float cx = sx + in * 5.2f * sc,  cy = 5.9f * sc, cz = 67.6f * sc;
  float dx = sx - in * 4.6f * sc,  dy = 6.9f * sc, dz = 71.4f * sc;

  setp(p);
  /* front and back wings of the cop */
  mq(base, MAT_IRON, ax, -ay, az, bx_, -by, bz, cx, -cy, cz, dx, -dy, dz);
  mq(base, MAT_IRON, ax,  ay, az, bx_,  by, bz, cx,  cy, cz, dx,  dy, dz);
  /* top plate tilted outwards, and the bottom lip */
  mq(base, MAT_IRON, ax, -ay, az, ax, ay, az, bx_, by, bz, bx_, -by, bz);
  mq(base, MAT_IRON, dx, -dy, dz, dx, dy, dz, cx, cy, cz, cx, -cy, cz);
  /* rolled upper lame so the cop does not look like a flat triangle */
  setc(172, 178, 190);
  mq(base, MAT_IRON, ax * 0.98f, -ay, az + 1.2f * sc,
     sx - in * 1.0f * sc, -6.9f * sc, 84.4f * sc,
     sx - in * 1.0f * sc,  6.9f * sc, 84.4f * sc,
     ax * 0.98f,  ay, az + 1.2f * sc);
  sett(p);
  mq(base, MAT_GOLD, dx, -dy, dz + 0.4f * sc, dx, dy, dz + 0.4f * sc,
     cx, cy, cz + 0.4f * sc, cx, -cy, cz + 0.4f * sc);
}

static void helm(Xf base, const Palette *p, float sc, int boss, int crest, int far)
{
  /* gorget */
  setl(p);
  bx(base, MAT_DARK, 0.0f, 0.0f, 80.8f * sc, 6.8f * sc, 6.6f * sc, 2.4f * sc, 0);

  /* skull, dome, cheek guards */
  setp(p);
  bx(base, MAT_IRON, 0.0f, 0.0f, 87.8f * sc, 8.0f * sc, 8.2f * sc, 6.2f * sc, 0);
  bx(base, MAT_IRON, 0.0f, 0.0f, 94.8f * sc, 6.5f * sc, 6.7f * sc, 2.2f * sc, 1);
  if (!far) {
    bx(base, MAT_IRON, -6.4f * sc, -1.2f * sc, 85.0f * sc, 1.2f * sc, 5.8f * sc, 4.0f * sc, 0);
    bx(base, MAT_IRON, 6.4f * sc, -1.2f * sc, 85.0f * sc, 1.2f * sc, 5.8f * sc, 4.0f * sc, 0);
  }

  /* face plate with a T slit: this dark band is what makes it read as a helm */
  setc(96, 94, 100);
  bx(base, MAT_IRON, 0.0f, -7.85f * sc, 87.4f * sc, 6.6f * sc, 0.9f * sc, 4.9f * sc, 0);
  setc(10, 10, 13);
  bx(base, MAT_DARK, 0.0f, -8.45f * sc, 88.6f * sc, 5.6f * sc, 0.5f * sc, 1.3f * sc, 0);
  if (!far) {
    bx(base, MAT_DARK, 0.0f, -8.45f * sc, 85.4f * sc, 1.1f * sc, 0.5f * sc, 2.8f * sc, 0);
    setc(150, 146, 146);
    bx(base, MAT_IRON, 0.0f, -7.6f * sc, 91.4f * sc, 5.0f * sc, 0.6f * sc, 0.9f * sc, 0);
  }

  if (boss) {
    sett(p);
    bx(base, MAT_GOLD, -8.8f * sc, 0.0f, 95.4f * sc, 1.5f * sc, 1.5f * sc, 5.0f * sc, 0);
    bx(base, MAT_GOLD, 8.8f * sc, 0.0f, 95.4f * sc, 1.5f * sc, 1.5f * sc, 5.0f * sc, 0);
    bx(base, MAT_GOLD, 0.0f, 0.0f, 96.8f * sc, 6.8f * sc, 6.8f * sc, 1.0f * sc, 0);
  } else if (crest) {
    setcl(p);
    bx(base, MAT_BANNER, 0.0f, 1.8f * sc, 97.4f * sc, 1.3f * sc, 4.6f * sc, 3.4f * sc, 0);
    bx(base, MAT_BANNER, 0.0f, 5.6f * sc, 94.6f * sc, 1.0f * sc, 1.8f * sc, 3.6f * sc, 0);
  } else {
    sett(p);
    bx(base, MAT_IRON, 0.0f, 0.6f * sc, 96.6f * sc, 1.0f * sc, 5.2f * sc, 1.7f * sc, 0);
  }
}

/* ------------------------------------------------------------------ fighter */

static void fighter(float px, float py, float yaw, float anim, float spd,
                    float atk, int dead, const Palette *p, float sc,
                    int boss, int has_shield, int far)
{
  Xf base = xf_move(xf_id(), px, py, 0.0f);
  float swing = sinf(anim) * (0.26f + 0.50f * spd);
  float bob = fabsf(sinf(anim)) * 2.2f * spd;
  float arm_r, arm_l, sword_a;
  float cape = 0.16f + 0.09f * sinf(anim * 0.5f + game_time * 1.3f) + 0.28f * spd;

  base = xf_rotz(base, yaw);
  base = xf_move(base, 0.0f, 0.0f, bob);
  if (dead) base = xf_pivot_rotx(base, -1.45f, 0.0f, 10.0f);

  arm_r = -0.26f - swing * 0.72f;
  arm_l = -0.26f + swing * 0.72f;
  sword_a = -1.05f;
  if (atk >= 0.0f) {
    if (atk < 0.30f) {
      float t = atk / 0.30f;
      arm_r = -0.26f - 2.15f * t;
      sword_a = -1.05f - 0.55f * t;
      arm_l = -0.26f + 0.60f * t;
    } else if (atk < 0.52f) {
      float t = (atk - 0.30f) / 0.22f;
      arm_r = -2.41f + 2.25f * t;
      sword_a = -1.60f + 1.35f * t;
      arm_l = 0.34f - 0.80f * t;
    } else {
      float t = (atk - 0.52f) / 0.48f;
      arm_r = -0.26f - 0.12f * (1.0f - t);
      sword_a = -0.25f - 0.80f * t;
      arm_l = -0.46f - 0.12f * (1.0f - t);
    }
  }

  /* cape first, so the body hides its root.  Three cloth panels, each with a
   * slightly different slant, read as folds instead of one flat card. */
  if (!far) {
    float w = 10.4f * sc;
    float yy = (9.0f + cape * 44.0f) * sc;
    float sw = 2.4f * sinf(game_time * 1.15f) * spd * sc;
    float bw = w * 1.30f;
    setcl(p);
    /* centre panel, bulging away from the back at the hem */
    mq(base, MAT_BANNER, -w * 0.34f, 6.6f * sc, 76.0f * sc,
       w * 0.34f, 6.6f * sc, 76.0f * sc,
       w * 0.38f + sw, yy + 5.0f * sc, 20.0f * sc,
       -w * 0.38f + sw, yy + 5.0f * sc, 20.0f * sc);
    /* left and right wings, curling forward at the hem so they fall on a
     * different plane and catch the moon light separately */
    mq(base, MAT_BANNER, -w, 6.5f * sc, 75.5f * sc,
       -w * 0.34f, 6.6f * sc, 76.0f * sc,
       -w * 0.38f + sw, yy + 5.0f * sc, 20.0f * sc,
       -bw + sw, yy - 6.0f * sc, 26.0f * sc);
    mq(base, MAT_BANNER, w * 0.34f, 6.6f * sc, 76.0f * sc,
       w, 6.5f * sc, 75.5f * sc,
       bw + sw, yy - 6.0f * sc, 26.0f * sc,
       w * 0.38f + sw, yy + 5.0f * sc, 20.0f * sc);
    /* a turned hem following the curve of the cloth */
    setl(p);
    mq(base, MAT_DARK, -bw + sw, yy - 6.0f * sc, 26.0f * sc,
       bw + sw, yy - 6.0f * sc, 26.0f * sc,
       bw * 0.80f + sw, yy + 5.0f * sc, 20.0f * sc,
       -bw * 0.80f + sw, yy + 5.0f * sc, 20.0f * sc);
  }

  leg(base, -8.2f * sc, swing, p, sc, far);
  leg(base, 8.2f * sc, -swing, p, sc, far);

  /* hips, fauld plates, belt */
  setl(p);
  bx(base, MAT_DARK, 0.0f, 0.0f, 51.0f * sc, 10.0f * sc, 7.0f * sc, 6.0f * sc, 0);
  setp(p);
  bx(base, MAT_IRON, 0.0f, -7.6f * sc, 47.0f * sc, 10.4f * sc, 1.4f * sc, 4.6f * sc, 0);
  bx(base, MAT_IRON, 0.0f, 7.6f * sc, 47.0f * sc, 10.4f * sc, 1.4f * sc, 4.6f * sc, 0);
  if (!far) {
    bx(base, MAT_IRON, -10.4f * sc, 0.0f, 47.0f * sc, 1.4f * sc, 7.2f * sc, 4.2f * sc, 0);
    bx(base, MAT_IRON, 10.4f * sc, 0.0f, 47.0f * sc, 1.4f * sc, 7.2f * sc, 4.2f * sc, 0);
  }
  sett(p);
  bx(base, MAT_GOLD, 0.0f, 0.0f, 55.0f * sc, 10.6f * sc, 7.4f * sc, 1.7f * sc, 0);

  /* torso: mail core, then a breastplate built from two plates that meet at
   * a raised keel, so the light breaks over a chest instead of a slab. */
  setl(p);
  bx(base, MAT_DARK, 0.0f, 0.0f, 62.0f * sc, 9.0f * sc, 6.6f * sc, 7.6f * sc, 0);
  bx(base, MAT_DARK, 0.0f, 0.0f, 72.6f * sc, 10.8f * sc, 7.4f * sc, 4.6f * sc, 0);

  setp(p);
  /* left and right breastplate wings, folded along the centre line */
  mq(base, MAT_IRON, -9.7f * sc, -6.5f * sc, 57.0f * sc,
     -11.2f * sc, -6.9f * sc, 77.4f * sc,
     0.0f, -10.1f * sc, 78.4f * sc,
     0.0f, -9.6f * sc, 56.0f * sc);
  mq(base, MAT_IRON,  9.7f * sc, -6.5f * sc, 57.0f * sc,
     11.2f * sc, -6.9f * sc, 77.4f * sc,
     0.0f, -10.1f * sc, 78.4f * sc,
     0.0f, -9.6f * sc, 56.0f * sc);
  /* side plates wrap the ribs */
  mq(base, MAT_IRON, -11.2f * sc, -6.9f * sc, 77.4f * sc,
     -11.2f * sc,  6.9f * sc, 77.4f * sc,
     -9.9f * sc,  6.3f * sc, 57.0f * sc,
     -9.7f * sc, -6.5f * sc, 57.0f * sc);
  mq(base, MAT_IRON,  11.2f * sc, -6.9f * sc, 77.4f * sc,
     11.2f * sc,  6.9f * sc, 77.4f * sc,
     9.9f * sc,  6.3f * sc, 57.0f * sc,
     9.7f * sc, -6.5f * sc, 57.0f * sc);
  /* backplate */
  mq(base, MAT_IRON, -11.0f * sc, 6.9f * sc, 77.4f * sc,
     11.0f * sc, 6.9f * sc, 77.4f * sc,
     9.7f * sc, 6.3f * sc, 57.0f * sc,
     -9.7f * sc, 6.3f * sc, 57.0f * sc);
  /* keel highlight and collar */
  setc(196, 202, 214);
  mq(base, MAT_IRON, -1.5f * sc, -10.0f * sc, 58.0f * sc,
     1.5f * sc, -10.0f * sc, 58.0f * sc,
     1.2f * sc, -10.4f * sc, 78.0f * sc,
     -1.2f * sc, -10.4f * sc, 78.0f * sc);
  sett(p);
  bx(base, MAT_GOLD, 0.0f, -8.6f * sc, 78.2f * sc, 6.6f * sc, 1.4f * sc, 1.0f * sc, 0);
  bx(base, MAT_GOLD, 0.0f, -8.2f * sc, 56.2f * sc, 2.2f * sc, 1.2f * sc, 1.8f * sc, 0);

  /* tabard: a narrow heraldic panel riding in front of the keel, plus the
   * back panel under the cape, both edged in brass */
  setcl(p);
  mq(base, MAT_BANNER, -5.5f * sc, -10.8f * sc, 78.0f * sc,
     5.5f * sc, -10.8f * sc, 78.0f * sc,
     4.7f * sc, -10.2f * sc, 37.0f * sc,
     -4.7f * sc, -10.2f * sc, 37.0f * sc);
  sett(p);
  mq(base, MAT_GOLD, -5.5f * sc, -11.0f * sc, 78.0f * sc,
     5.5f * sc, -11.0f * sc, 78.0f * sc,
     5.1f * sc, -11.0f * sc, 75.6f * sc,
     -5.1f * sc, -11.0f * sc, 75.6f * sc);
  setcl(p);
  mq(base, MAT_BANNER, -6.8f * sc, 9.2f * sc, 76.0f * sc,
     6.8f * sc, 9.2f * sc, 76.0f * sc,
     5.6f * sc, 9.8f * sc, 40.0f * sc,
     -5.6f * sc, 9.8f * sc, 40.0f * sc);

  pauldron(base, -13.2f * sc, p, sc);
  pauldron(base, 13.2f * sc, p, sc);

  helm(base, p, sc, boss, !boss, far);

  /* arms: right carries the sword, left a shield (or a free hand) */
  {
    Xf ar = arm_frame(base, 13.2f * sc, arm_r, sc);
    Xf al = arm_frame(base, -13.2f * sc, arm_l, sc);
    arm(ar, p, sc, far);
    arm(al, p, sc, far);
    if (has_shield) {
      shield(al, p, sc);
    } else if (!far) {
      sett(p);
      bx(al, MAT_IRON, 0.0f, -3.0f * sc, -33.0f * sc, 3.0f * sc, 3.2f * sc, 3.4f * sc, 0);
    }
    {
      Xf sf = xf_move(ar, 0.0f, -1.0f * sc, -33.0f * sc);
      sf = xf_pivot_rotx(sf, sword_a, 0.0f, 0.0f);
      sword(sf, p, sc);
    }
  }

  shadow_blob(px, py, 23.0f * sc, 16.0f * sc, dead ? 26 : 48);
}

/* -------------------------------------------------------------------- npcs */

typedef struct {
  float x, y, yaw, phase, range, speed;
  const Palette *pal;
} Npc;

static const Npc npcs[] = {
  { 872.0f, 2418.0f, 0.4f, 0.0f, 26.0f, 0.22f, &pal_vill_a },
  { 962.0f, 2562.0f, 2.6f, 1.7f, 20.0f, 0.16f, &pal_vill_b },
  { 1014.0f, 2472.0f, 4.1f, 3.2f, 18.0f, 0.19f, &pal_vill_c },
  { 846.0f, 2664.0f, 1.2f, 4.6f, 22.0f, 0.14f, &pal_vill_a },
  { 902.0f, 1758.0f, 3.6f, 2.4f, 16.0f, 0.12f, &pal_vill_b },
  { 818.0f, 3372.0f, 0.9f, 5.4f, 20.0f, 0.18f, &pal_vill_c },
  { 896.0f, 3452.0f, 2.2f, 0.8f, 18.0f, 0.15f, &pal_vill_a }
};
#define NPC_COUNT ((int)(sizeof(npcs) / sizeof(npcs[0])))

static void villager(const Npc *n, float t, float px, float py, float yaw)
{
  const Palette *p = n->pal;
  float walk = sinf(t * 2.0f + n->phase);
  float bob = fabsf(walk) * 1.5f;
  float step = walk * 0.55f;
  Xf base = xf_move(xf_id(), px, py, bob);
  Xf la, ra;

  base = xf_rotz(base, yaw);

  /* legs: dark hose that the tunic hem hangs over, swinging from the hip */
  {
    Xf ll = xf_pivot_rotx(xf_move(base, -4.6f, 0.0f, 22.0f), -step * 0.55f, 0.0f, 0.0f);
    Xf rl = xf_pivot_rotx(xf_move(base,  4.6f, 0.0f, 22.0f),  step * 0.55f, 0.0f, 0.0f);
    setl(p);
    bx(ll, MAT_DARK, 0.0f, 0.0f, -12.0f, 3.4f, 4.2f, 10.0f, 0);
    bx(rl, MAT_DARK, 0.0f, 0.0f, -12.0f, 3.4f, 4.2f, 10.0f, 0);
    setc(46, 38, 30);
    bx(ll, MAT_DARK, 0.0f, -1.8f, -21.2f, 3.7f, 5.4f, 1.7f, 1);
    bx(rl, MAT_DARK, 0.0f, -1.8f, -21.2f, 3.7f, 5.4f, 1.7f, 1);
  }

  /* tunic: hem, skirt, chest, then a shoulder mantle */
  setcl(p);
  bx(base, MAT_LIME, 0.0f, 0.0f, 12.0f, 10.0f, 7.4f, 10.0f, 0);
  bx(base, MAT_LIME, 0.0f, 0.0f, 30.0f, 9.6f, 7.2f, 11.0f, 0);
  bx(base, MAT_LIME, 0.0f, 0.0f, 44.0f, 8.6f, 6.4f, 7.4f, 0);
  /* rope belt and hem band */
  sett(p);
  bx(base, MAT_PLANK, 0.0f, 0.0f, 24.0f, 10.0f, 7.6f, 1.3f, 0);
  setc(58, 50, 40);
  bx(base, MAT_DARK, 0.0f, 0.0f, 3.4f, 11.3f, 8.3f, 1.4f, 0);
  /* mantle: a sloped shoulder cape so the silhouette is not a cylinder */
  setcl(p);
  mq(base, MAT_LIME, -7.6f, -6.6f, 52.4f, 7.6f, -6.6f, 52.4f,
     11.0f, -7.4f, 40.0f, -11.0f, -7.4f, 40.0f);
  mq(base, MAT_LIME, -7.6f, 6.6f, 52.4f, 7.6f, 6.6f, 52.4f,
     11.0f, 7.4f, 40.0f, -11.0f, 7.4f, 40.0f);
  mq(base, MAT_LIME, -7.6f, -6.6f, 52.4f, -7.6f, 6.6f, 52.4f,
     -11.0f, 7.4f, 40.0f, -11.0f, -7.4f, 40.0f);
  mq(base, MAT_LIME, 7.6f, -6.6f, 52.4f, 7.6f, 6.6f, 52.4f,
     11.0f, 7.4f, 40.0f, 11.0f, -7.4f, 40.0f);

  /* head: a face under a deep hood, with a little hair or a beard */
  setc(255, 226, 198);
  bx(base, MAT_LIME, 0.0f, -2.4f, 57.0f, 3.9f, 3.6f, 4.4f, 0);
  setl(p);
  bx(base, MAT_DARK, 0.0f, -3.4f, 53.6f, 3.3f, 2.6f, 1.5f, 0);
  setc(30, 26, 30);
  bx(base, MAT_DARK, -1.4f, -5.9f, 58.0f, 0.7f, 0.4f, 0.6f, 0);
  bx(base, MAT_DARK,  1.4f, -5.9f, 58.0f, 0.7f, 0.4f, 0.6f, 0);
  setcl(p);
  bx(base, MAT_LIME, 0.0f, 1.2f, 58.6f, 5.0f, 5.0f, 5.2f, 1);
  bx(base, MAT_LIME, 0.0f, 1.6f, 62.8f, 3.7f, 3.7f, 1.6f, 1);
  mq(base, MAT_LIME, -5.0f, -3.6f, 60.2f, -5.0f, -3.6f, 54.6f,
     -3.0f, -5.4f, 55.4f, -3.0f, -5.4f, 59.4f);
  mq(base, MAT_LIME, 5.0f, -3.6f, 60.2f, 5.0f, -3.6f, 54.6f,
     3.0f, -5.4f, 55.4f, 3.0f, -5.4f, 59.4f);

  /* arms: right swings freely, left carries a lantern */
  la = xf_pivot_rotx(xf_move(base, -8.8f, 0.0f, 47.0f), -0.95f + step * 0.25f, 0.0f, 0.0f);
  ra = xf_pivot_rotx(xf_move(base,  9.2f, 0.0f, 47.0f), -0.34f - step * 0.4f, 0.0f, 0.0f);
  setcl(p);
  bx(ra, MAT_LIME, 0.0f, 0.0f, -5.0f, 3.0f, 3.4f, 6.0f, 0);
  bx(ra, MAT_LIME, 0.0f, 0.0f, -12.6f, 2.6f, 3.0f, 3.6f, 0);
  setc(255, 222, 190);
  bx(ra, MAT_LIME, 0.0f, -0.4f, -17.0f, 2.4f, 2.6f, 2.0f, 0);
  setcl(p);
  bx(la, MAT_LIME, 0.0f, 0.0f, -5.0f, 3.0f, 3.4f, 6.0f, 0);
  bx(la, MAT_LIME, 0.0f, -1.0f, -12.6f, 2.6f, 3.0f, 4.0f, 0);
  setc(255, 222, 190);
  bx(la, MAT_LIME, 0.0f, -2.6f, -16.6f, 2.1f, 2.1f, 1.8f, 0);
  /* lantern: iron frame, lit panes, and a light in the hand */
  {
    V3 L = xf_pt(la, 0.0f, -2.6f, -21.0f);
    sett(p);
    bx(la, MAT_IRON, 0.0f, -2.6f, -21.2f, 2.4f, 2.4f, 2.8f, 1);
    setc(255, 208, 132);
    bx(la, MAT_DARK, 0.0f, -2.6f, -21.2f, 1.7f, 1.7f, 2.1f, 0);
    glow_billboard(L.x, L.y, L.z, 12.0f, 255, 206, 136, 104);
    glow_billboard(L.x, L.y, L.z, 34.0f, 255, 168, 88, 30);
  }
  shadow_blob(px, py, 15.0f, 11.0f, 42);
}

/* the two nearest villagers carry a lit lantern, so the lane is not a row of
 * black silhouettes: cheap, and it makes the town feel lived in */
void npcs_lights(float cx, float cy)
{
  int i, added = 0;
  (void)cx; (void)cy;
  for (i = 0; i < NPC_COUNT && added < 2; i++) {
    const Npc *n = &npcs[i];
    float t = game_time * n->speed + n->phase;
    float off = sinf(t) * n->range;
    float px = n->x + cosf(n->yaw) * off;
    float py = n->y + sinf(n->yaw) * off;
    float dx = px - cam.x, dy = py - cam.y;
    float d = sqrtf(dx * dx + dy * dy) + 0.001f;
    if (d > 820.0f) continue;
    /* a soft key hung between the viewer and the figure, exactly like the one
     * the player gets, so a villager reads as a person and not a silhouette */
    dx /= d; dy /= d;
    dyn_light_add(px - dx * 46.0f, py - dy * 46.0f, 66.0f, 235.0f, 0.32f,
                  246, 226, 198);
    /* and the lantern itself, warm, at hip height */
    dyn_light_add(px + 6.0f * sinf(n->yaw) - 8.8f * cosf(n->yaw),
                  py - 6.0f * cosf(n->yaw) - 8.8f * sinf(n->yaw), 40.0f,
                  150.0f, 0.45f, 255, 196, 124);
    added++;
  }
}

void npcs_draw(void)
{
  int i;
  for (i = 0; i < NPC_COUNT; i++) {
    const Npc *n = &npcs[i];
    float t = game_time * n->speed + n->phase;
    float off = sinf(t) * n->range;
    float px = n->x + cosf(n->yaw) * off;
    float py = n->y + sinf(n->yaw) * off;
    float vx = cosf(n->yaw) * cosf(t), vy = sinf(n->yaw) * cosf(t);
    float yaw = n->yaw + (vx * vx + vy * vy > 0.001f ? 0.0f : 3.14159f);
    if (dist2(px, py, cam.x, cam.y) > 1500.0f * 1500.0f) continue;
    villager(n, t, px, py, yaw);
  }
}

/* ------------------------------------------------------------------- draw */

void actors_draw(void)
{
  int i;

  for (i = 0; i < enemy_count; i++) {
    Enemy *e = &enemies[i];
    float spd, atk = -1.0f, d2;

    if (!e->alive && e->dead_t > 2.4f) continue;
    d2 = dist2(e->x, e->y, cam.x, cam.y);
    if (d2 > 1700.0f * 1700.0f) continue;

    flashmix = (e->hurt > 0.0f) ? 1 : 0;
    spd = (e->state == 1) ? 1.0f : 0.0f;
    if (e->state == 2)
      atk = (1.0f - e->windup / 0.45f) * 0.29f;
    else if (e->state == 3)
      atk = 0.30f + (1.0f - e->strike / 0.24f) * 0.69f;
    if (atk < 0.0f) atk = -1.0f;
    if (atk > 0.98f) atk = 0.98f;

    fighter(e->x, e->y, e->yaw, e->anim, spd, atk,
            (!e->alive && e->dead_t > 0.4f),
            e->type ? &pal_boss : &pal_enemy,
            e->type ? 1.30f : 1.0f, e->type, (i & 1), d2 > 620.0f * 620.0f);
  }

  flashmix = (player.hurt_t > 0.0f) ? 1 : 0;
  fighter(player.x, player.y, player.yaw, player.anim,
          player.running ? 1.0f : (player.moving ? 0.55f : 0.0f),
          player.attacking ? player.atk_t : -1.0f,
          player.dead, &pal_player, 1.0f, 0, 0, 0);
  flashmix = 0;
}
