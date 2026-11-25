/* THE ASHEN KNIGHT — world.
 *
 * One map holds four connected places: the castle courtyard, the stone bridge
 * over the gorge, the town and the harbour.  Ground is a tile grid; everything
 * tall is a "prop" so collision, culling and lighting stay cheap.
 */
#include <math.h>
#include <string.h>
#include "game.h"

unsigned char tiles[MAP_H][MAP_W];
float water_time;

Prop props[MAX_PROPS];
int prop_count;

Light lights[MAX_LIGHTS];
int light_count;

/* ------------------------------------------------------------------ noise */

static unsigned int h32(unsigned int x)
{
  x ^= x >> 16; x *= 0x7feb352du;
  x ^= x >> 15; x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

static float frnd(int a, int b)
{
  return (float)(h32((unsigned int)(a * 73856093) ^ (unsigned int)(b * 19349663)) & 1023u) / 1023.0f;
}

/* ------------------------------------------------------------------- tiles */

static void fill_rect(int x0, int y0, int x1, int y1, int t)
{
  int x, y;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > MAP_W) x1 = MAP_W;
  if (y1 > MAP_H) y1 = MAP_H;
  for (y = y0; y < y1; y++)
    for (x = x0; x < x1; x++)
      tiles[y][x] = (unsigned char)t;
}

int tile_solid_at(float x, float y)
{
  int tx = (int)(x / TILE), ty = (int)(y / TILE);
  if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return 1;
  {
    int t = tiles[ty][tx];
    return t == T_WATER || t == T_VOID;
  }
}

int prop_blocks(float x, float y, float r)
{
  int i;
  for (i = 0; i < prop_count; i++) {
    Prop *p = &props[i];
    float hw, hd;
    switch (p->type) {
      case P_GATE: case P_PIER: case P_BRAZIER: case P_POST: case P_BANNER:
        continue;
      case P_BOAT:
        hw = p->hw + 10; hd = p->hd + 10;
        break;
      default:
        hw = p->hw; hd = p->hd;
        break;
    }
    if (x > p->x - hw - r && x < p->x + hw + r &&
        y > p->y - hd - r && y < p->y + hd + r)
      return 1;
  }
  return 0;
}

/* ------------------------------------------------------------------ lights */

void light_add(float x, float y, float z, float r, float cr, float cg, float cb, float k)
{
  Light *l;
  if (light_count >= MAX_LIGHTS) return;
  l = &lights[light_count++];
  l->x = x; l->y = y; l->z = z; l->r = r;
  l->cr = cr; l->cg = cg; l->cb = cb;
  l->k = k;
  l->phase = frnd((int)x, (int)y) * 6.28318531f;
}

/* Ambient + a cool moon key + the torch pool. */
#define MAX_DYNL 8
static float dlx[MAX_DYNL], dly[MAX_DYNL], dlz[MAX_DYNL], dlr[MAX_DYNL], dlk[MAX_DYNL];
static int dlcr[MAX_DYNL], dlg[MAX_DYNL], dlb[MAX_DYNL];
static int dln;

void dyn_light_clear(void) { dln = 0; }

void dyn_light_add(float x, float y, float z, float r, float k, int cr, int cg, int cb)
{
  if (dln >= MAX_DYNL) return;
  dlx[dln] = x; dly[dln] = y; dlz[dln] = z;
  dlr[dln] = r; dlk[dln] = k;
  dlcr[dln] = cr; dlg[dln] = cg; dlb[dln] = cb;
  dln++;
}

void light_rgb(float x, float y, float z, float nx, float ny, float nz,
               float *or_, float *og, float *ob)
{
  float r, g, b, nd, t;
  int i;

  nd = nx * 0.34f + ny * -0.28f + nz * 0.90f;
  if (nd < 0.0f) nd = 0.0f;

  t = nz > 0.0f ? nz : 0.0f;
  r = 0.20f + 0.05f * t;
  g = 0.21f + 0.06f * t;
  b = 0.27f + 0.09f * t;
  r += 0.40f * nd;
  g += 0.43f * nd;
  b += 0.55f * nd;

  for (i = 0; i < light_count; i++) {
    Light *l = &lights[i];
    float dx = l->x - x, dy = l->y - y, dz = l->z - z;
    float d2 = dx * dx + dy * dy + dz * dz;
    float r2 = l->r * l->r;
    float att, ndl, d;
    if (d2 > r2) continue;
    d = sqrtf(d2) + 0.001f;
    att = 1.0f - d / l->r;
    att *= att;
    att *= l->k * (0.86f + 0.14f * sinf(game_time * 6.3f + l->phase));
    ndl = (dx * nx + dy * ny + dz * nz) / d;
    ndl = 0.28f + 0.72f * (ndl > 0.0f ? ndl : 0.0f);
    r += l->cr * att * ndl;
    g += l->cg * att * ndl;
    b += l->cb * att * ndl;
  }

  for (i = 0; i < dln; i++) {
    float dx = dlx[i] - x, dy = dly[i] - y, dz = dlz[i] - z;
    float d2 = dx * dx + dy * dy + dz * dz;
    float r2 = dlr[i] * dlr[i];
    float att, ndl, d;
    if (d2 > r2) continue;
    d = sqrtf(d2) + 0.001f;
    att = 1.0f - d / dlr[i];
    att *= att;
    att *= dlk[i];
    ndl = (dx * nx + dy * ny + dz * nz) / d;
    ndl = 0.35f + 0.65f * (ndl > 0.0f ? ndl : 0.0f);
    r += (float)dlcr[i] / 255.0f * att * ndl;
    g += (float)dlg[i] / 255.0f * att * ndl;
    b += (float)dlb[i] / 255.0f * att * ndl;
  }

  /* soft bounce from the viewer: keeps the knight readable against the dark */
  {
    float dx = cam.x - x, dy = cam.y - y, dz = cam.z - z;
    float d2 = dx * dx + dy * dy + dz * dz;
    float att = 780.0f / (780.0f + d2 * 0.02f);
    float ndl = (dx * nx + dy * ny + dz * nz * 0.5f) / (sqrtf(d2) + 1.0f);
    ndl = 0.40f + 0.60f * (ndl > 0.0f ? ndl : 0.0f);
    att *= ndl;
    r += 0.31f * att;
    g += 0.35f * att;
    b += 0.45f * att;
  }

  *or_ = r; *og = g; *ob = b;
}

/* -------------------------------------------------------------- prop list */

static Prop *add_prop(int type, float x, float y, float z, float hw, float hd,
                      float h, int sub)
{
  Prop *p;
  if (prop_count >= MAX_PROPS) return 0;
  p = &props[prop_count++];
  p->type = (unsigned char)type;
  p->x = (short)x; p->y = (short)y; p->z = (short)z;
  p->hw = (short)hw; p->hd = (short)hd; p->h = (short)h;
  p->sub = (unsigned char)sub;
  return p;
}

static void brazier(float x, float y)
{
  add_prop(P_BRAZIER, x, y, 0, 12, 12, 74, 0);
  light_add(x, y, 84.0f, 320.0f, 1.0f, 0.52f, 0.16f, 0.92f);
}

static void post(float x, float y, float z)
{
  add_prop(P_POST, x, y, (int)z, 8, 8, 96, 0);
  light_add(x, y, z + 112.0f, 320.0f, 1.0f, 0.60f, 0.22f, 0.95f);
}

static void banner(float x, float y, int dir)
{
  add_prop(P_BANNER, x, y, 0, 26, 4, 96, dir);
}

/* ------------------------------------------------------------------- gen */

void world_gen(void)
{
  int i;
  float y;

  prop_count = 0;
  light_count = 0;
  water_time = 0.0f;
  memset(tiles, T_MOSS, sizeof(tiles));

  /* ---- ground layout ---- */
  /* castle courtyard */
  fill_rect(8, 4, 22, 21, T_FLAG);
  /* the approach road and the bridge deck */
  fill_rect(12, 20, 16, 23, T_FLAG);
  fill_rect(11, 23, 16, 31, T_FLAG);
  fill_rect(12, 31, 17, 34, T_FLAG);
  /* town */
  fill_rect(4, 34, 27, 51, T_COBBLE);
  fill_rect(12, 33, 17, 35, T_COBBLE);
  /* dock + sea */
  fill_rect(4, 51, 28, 56, T_PLANK);
  fill_rect(0, 56, 64, 64, T_WATER);
  /* the river gorge east-west */
  fill_rect(0, 23, 64, 31, T_WATER);
  fill_rect(11, 23, 16, 31, T_FLAG);      /* bridge deck over the river */
  /* banks: a little earth on both shores reads better than moss to water */
  fill_rect(0, 21, 64, 23, T_EARTH);
  fill_rect(0, 31, 64, 33, T_EARTH);
  /* north outside the castle is dead ground */
  fill_rect(0, 0, 64, 4, T_EARTH);

  /* ---- castle ---- */
  /* south curtain wall, split around the gate at x 832..960 */
  add_prop(P_WALL, 664, 1300, 0, 168, 22, 128, 0);
  add_prop(P_WALL, 1208, 1300, 0, 248, 22, 128, 0);
  add_prop(P_WALL, 960, 276, 0, 460, 22, 128, 0);
  add_prop(P_WALL, 512, 788, 0, 22, 512, 128, 1);
  add_prop(P_WALL, 1408, 788, 0, 22, 512, 128, 1);
  add_prop(P_GATE, 896, 1300, 0, 64, 22, 150, 0);
  add_prop(P_TOWER, 512, 276, 0, 62, 62, 226, 1);
  add_prop(P_TOWER, 1408, 276, 0, 62, 62, 226, 1);
  add_prop(P_TOWER, 512, 1300, 0, 62, 62, 206, 1);
  add_prop(P_TOWER, 1408, 1300, 0, 62, 62, 206, 1);
  add_prop(P_TOWER, 800, 1300, 0, 48, 48, 186, 0);
  add_prop(P_TOWER, 992, 1300, 0, 48, 48, 186, 0);
  add_prop(P_TOWER, 760, 620, 0, 104, 104, 320, 2);   /* the keep */
  add_prop(P_TOWER, 1180, 560, 0, 54, 54, 250, 1);

  brazier(700, 1140);
  brazier(1100, 1140);
  brazier(700, 760);
  brazier(1100, 760);
  brazier(896, 1370);
  banner(760, 300, 1);
  banner(960, 300, 1);
  banner(1160, 300, 1);
  add_prop(P_BARREL, 660, 1060, 0, 20, 20, 44, 0);
  add_prop(P_BARREL, 700, 1080, 0, 20, 20, 44, 0);
  add_prop(P_CRATE, 1200, 1000, 0, 30, 30, 40, 0);
  add_prop(P_CRATE, 1160, 1050, 0, 26, 26, 34, 0);

  /* ---- bridge ---- */
  add_prop(P_WALL, 704, 1728, 0, 14, 258, 34, 1);
  add_prop(P_WALL, 1024, 1728, 0, 14, 258, 34, 1);
  post(736, 1570, 0);
  post(992, 1570, 0);
  post(736, 1900, 0);
  post(992, 1900, 0);

  /* ---- town ---- */
  for (i = 0; i < 5; i++) {
    y = 2250.0f + i * 210.0f;
    add_prop(P_HOUSE, 520, (int)y, 0, 92, 86, 150 + (int)(frnd(i, 1) * 60.0f), i & 3);
    add_prop(P_HOUSE, 1272, (int)(y + 100.0f), 0, 92, 86, 150 + (int)(frnd(i, 7) * 60.0f), (i + 2) & 3);
  }
  add_prop(P_STATUE, 896, 2700, 0, 46, 46, 168, 0);
  /* two fires flanking the plinth so the monument is lit from below */
  brazier(806, 2692);
  brazier(986, 2692);
  brazier(800, 2560);
  brazier(992, 2560);
  brazier(800, 2860);
  brazier(992, 2860);
  brazier(700, 2300);
  brazier(1120, 2300);
  add_prop(P_CART, 700, 2440, 0, 46, 26, 44, 0);
  add_prop(P_CART, 1100, 2960, 0, 46, 26, 44, 1);
  add_prop(P_CART, 640, 3100, 0, 46, 26, 44, 0);
  add_prop(P_BARREL, 1030, 2320, 0, 20, 20, 44, 0);
  add_prop(P_BARREL, 1064, 2344, 0, 18, 18, 40, 0);
  add_prop(P_BARREL, 760, 3040, 0, 20, 20, 44, 0);
  add_prop(P_CRATE, 1180, 2400, 0, 28, 28, 38, 0);
  add_prop(P_CRATE, 1216, 2432, 0, 24, 24, 32, 0);
  add_prop(P_CRATE, 1080, 3120, 0, 30, 30, 40, 0);
  banner(1230, 2210, 0);
  banner(560, 2210, 0);
  post(1180, 2650, 0);
  post(620, 2760, 0);

  /* ---- harbour ---- */
  add_prop(P_PIER, 500, 3700, 0, 34, 200, 10, 0);
  add_prop(P_PIER, 896, 3740, 0, 34, 210, 10, 0);
  add_prop(P_PIER, 1300, 3700, 0, 34, 200, 10, 0);
  add_prop(P_BOAT, 640, 3690, 0, 62, 30, 60, 0);
  add_prop(P_BOAT, 1030, 3760, 0, 62, 30, 60, 1);
  add_prop(P_BOAT, 1440, 3660, 0, 54, 28, 52, 0);
  add_prop(P_CART, 760, 3380, 0, 46, 26, 44, 1);
  add_prop(P_CRATE, 900, 3390, 0, 28, 28, 38, 0);
  add_prop(P_CRATE, 936, 3420, 0, 24, 24, 32, 0);
  add_prop(P_CRATE, 1200, 3360, 0, 28, 28, 38, 0);
  add_prop(P_BARREL, 400, 3400, 0, 20, 20, 44, 0);
  add_prop(P_BARREL, 436, 3430, 0, 18, 18, 40, 0);
  post(760, 3560, 0);
  post(1100, 3560, 0);
  post(420, 3560, 0);
  add_prop(P_TOWER, 1640, 3400, 0, 72, 72, 300, 2);  /* lighthouse */

  /* fog starts dark and cool; game.c retints per zone */
  fog_r = 0.07f; fog_g = 0.08f; fog_b = 0.11f;
  fog_near = 620.0f; fog_far = 2150.0f;
}

/* ---------------------------------------------------------------- drawing */

static int tile_mat(int t)
{
  switch (t) {
    case T_COBBLE: return MAT_COBBLE;
    case T_FLAG:   return MAT_FLAG;
    case T_PLANK:  return MAT_PLANK;
    case T_EARTH:  return MAT_EARTH;
    case T_MOSS:   return MAT_MOSS;
    case T_WATER:  return MAT_WATER0;
    case T_VOID:   return MAT_DARK;
  }
  return MAT_MOSS;
}

/* flagstones and cobbles were reading as pale paper under the moon: keep
 * them a shade darker than the walls so the masonry still reads */
static float tile_tint(int t)
{
  switch (t) {
    case T_FLAG:   return 0.74f;
    case T_COBBLE: return 0.88f;
    case T_PLANK:  return 0.90f;
    case T_EARTH:  return 0.96f;
    default:       return 1.0f;
  }
}

/* ground quad for one tile, optionally raised/lowered */
static void ground_quad(int tx, int ty, int t, float z, float size)
{
  float x0 = tx * (float)TILE, y0 = ty * (float)TILE;
  float x1 = x0 + size, y1 = y0 + size;
  float u0, v0, u1, v1;
  int mat = tile_mat(t);
  float var = (0.86f + frnd(tx, ty) * 0.09f) * tile_tint(t);

  if (t == T_WATER) {
    int f = (int)(water_time * 5.0f + frnd(tx, ty) * 4.0f) & 3;
    mat = MAT_WATER0 + f;
    var = 0.96f + 0.06f * sinf(water_time * 1.7f + (tx + ty) * 0.7f);
  }
  cell_rect(mat, &u0, &v0, &u1, &v1);
  /* show a random quarter of the material cell, mirrored at random: sixteen
   * variants per material, so the ground never repeats on one tile rhythm */
  if (t != T_WATER) {
    float w = (u1 - u0) * 0.5f, h = (v1 - v0) * 0.5f;
    float z0 = frnd(tx * 7 + 1, ty * 3 + 5), z1 = frnd(tx * 3 + 4, ty * 5 + 2);
    float z2 = frnd(tx * 5 + 2, ty * 11 + 7), z3 = frnd(tx * 13 + 3, ty * 2 + 9);
    float a0 = u0 + (z2 > 0.5f ? w : 0.0f);
    float b0 = v0 + (z3 > 0.5f ? h : 0.0f);
    u0 = a0; u1 = a0 + w; v0 = b0; v1 = b0 + h;
    if (z0 > 0.5f) { float uu = u0; u0 = u1; u1 = uu; }
    if (z1 > 0.5f) { float vv = v0; v0 = v1; v1 = vv; }
  }
  mesh_tint((int)(var * 255.0f), (int)(var * 255.0f), (int)(var * 255.0f));
  qface(mat, x0, y0, z, x1, y0, z, x1, y1, z, x0, y1, z, u0, v0, u1, v1);

  /* river / sea bank: vertical skirt on any edge that drops to water */
  if (t != T_WATER && z > -1.0f) {
    float dz = -26.0f;
    mesh_tint(150, 150, 158);
    if (ty > 0 && tiles[ty - 1][tx] == T_WATER)
      qcell(MAT_DARK, x0, y0, z, x1, y0, z, x1, y0, dz, x0, y0, dz);
    if (ty < MAP_H - 1 && tiles[ty + 1][tx] == T_WATER)
      qcell(MAT_DARK, x1, y1, z, x0, y1, z, x0, y1, dz, x1, y1, dz);
    if (tx > 0 && tiles[ty][tx - 1] == T_WATER)
      qcell(MAT_DARK, x0, y1, z, x0, y0, z, x0, y0, dz, x0, y1, dz);
    if (tx < MAP_W - 1 && tiles[ty][tx + 1] == T_WATER)
      qcell(MAT_DARK, x1, y0, z, x1, y1, z, x1, y1, dz, x1, y0, dz);
  }
}

static void ground_ring(int step, float dnear, float dfar)
{
  int tx, ty, n;
  float sx, sy, dep, size = (float)(TILE * step);
  for (ty = 0; ty < MAP_H; ty += step) {
    for (tx = 0; tx < MAP_W; tx += step) {
      int t = tiles[ty][tx];
      float wx = tx * TILE + size * 0.5f, wy = ty * TILE + size * 0.5f;
      if (!proj(wx, wy, 0.0f, &sx, &sy, &dep)) continue;
      if (dep < dnear || dep > dfar) continue;
      if (sx < -260.0f - size || sx > SCR_W + 260.0f + size) continue;
      if (sy < -200.0f - size || sy > SCR_H + 260.0f + size) continue;
      if (step > 1 && (ty + step / 2 < MAP_H)) t = tiles[ty + step / 2][tx];
      if (t == T_VOID) continue;
      n = step;
      (void)n;
      if (step == 1) {
        ground_quad(tx, ty, t, t == T_WATER ? -26.0f : 0.0f, (float)TILE);
      } else {
        float u0, v0, u1, v1, var;
        int mat = tile_mat(t);
        if (t == T_WATER)
          mat = MAT_WATER0 + ((int)(water_time * 4.0f + frnd(tx, ty) * 4.0f) & 3);
        var = (0.72f + frnd(tx + 3, ty + 5) * 0.14f) * tile_tint(t);
        cell_rect(mat, &u0, &v0, &u1, &v1);
        mesh_tint((int)(var * 255.0f), (int)(var * 255.0f), (int)(var * 255.0f));
        qface(mat, (float)(tx * TILE), (float)(ty * TILE), t == T_WATER ? -26.0f : 0.0f,
              (float)(tx * TILE) + size, (float)(ty * TILE), t == T_WATER ? -26.0f : 0.0f,
              (float)(tx * TILE) + size, (float)(ty * TILE) + size, t == T_WATER ? -26.0f : 0.0f,
              (float)(tx * TILE), (float)(ty * TILE) + size, t == T_WATER ? -26.0f : 0.0f,
              u0, v0, u1, v1);
      }
    }
  }
  mesh_tint(255, 255, 255);
}

void world_draw_ground(void)
{
  water_time = game_time;
  ground_ring(1, 0.0f, 760.0f);
  ground_ring(2, 760.0f, 1500.0f);
  ground_ring(4, 1500.0f, 2700.0f);
}

/* ------------------------------------------------------------ prop models */

static void lq(Xf f, int mat, float ax, float ay, float az, float bx, float by,
               float bz, float cx, float cy, float cz, float dx, float dy, float dz)
{
  V3 A = xf_pt(f, ax, ay, az), B = xf_pt(f, bx, by, bz);
  V3 C = xf_pt(f, cx, cy, cz), D = xf_pt(f, dx, dy, dz);
  qcell(mat, A.x, A.y, A.z, B.x, B.y, B.z, C.x, C.y, C.z, D.x, D.y, D.z);
}

/* battlement teeth along local x from x0..x1, at depth y0..y1, on top of z */
static void merlons(Xf f, int mat, float x0, float x1, float y0, float y1, float z)
{
  int i, n = 5;
  float span = x1 - x0;
  float w = span / (float)(n * 2);
  for (i = 0; i < n; i++) {
    float a = x0 + (float)(i * 2) * w;
    float b = a + w;
    /* both faces read, so one quad each side plus a cap */
    lq(f, mat, a, y1, z, b, y1, z, b, y1, z + 26.0f, a, y1, z + 26.0f);
    lq(f, mat, b, y0, z, a, y0, z, a, y0, z + 26.0f, b, y0, z + 26.0f);
    lq(f, mat, a, y0, z + 26.0f, b, y0, z + 26.0f, b, y1, z + 26.0f, a, y1, z + 26.0f);
  }
}

static float prop_lod = 0.0f;

static void draw_wall(Xf f, Prop *p)
{
  float len = (float)p->hw, thick = (float)p->hd, h = (float)p->h;
  int segs, i;
  float step;

  /* the collision box is axis aligned; build along local x and spin the frame
   * for north-south runs */
  if (p->hd > p->hw) {
    f = xf_rotz(f, 1.57079633f);
    len = (float)p->hd;
    thick = (float)p->hw;
  }
  segs = (int)(len * 2.0f / 84.0f) + 1;
  step = (2.0f * len) / (float)segs;

  for (i = 0; i < segs; i++) {
    float a = -len + step * i, b = a + step;
    lq(f, MAT_STONE, a, thick, 0, b, thick, 0, b, thick, h, a, thick, h);
    lq(f, MAT_STONE, b, -thick, 0, a, -thick, 0, a, -thick, h, b, -thick, h);
  }
  lq(f, MAT_STONE, -len, -thick, h, len, -thick, h, len, thick, h, -len, thick, h);
  if (prop_lod < 1250.0f)
    merlons(f, MAT_STONE, -len + 8.0f, len - 8.0f, -thick, thick, h);

  /* a dark embrasure slit */
  if (prop_lod > 1000.0f) { mesh_tint(255, 255, 255); return; }
  mesh_tint(150, 132, 108);
  lq(f, MAT_DARK, -len * 0.35f, thick + 0.6f, h * 0.45f,
     -len * 0.15f, thick + 0.6f, h * 0.45f,
     -len * 0.15f, thick + 0.6f, h * 0.45f + 26.0f,
     -len * 0.35f, thick + 0.6f, h * 0.45f + 26.0f);
  mesh_tint(255, 255, 255);
}

static void draw_tower(Xf f, Prop *p)
{
  float r = (float)(p->hw + p->hd) * 0.5f;
  float h = (float)p->h;
  int sides = 10, i;
  float ang;
  float ry = (float)p->hd;

  /* shaft: stacked boxes so the masonry pattern repeats up the tower instead
   * of stretching one brick course over the whole height */
  {
    int nz = (int)(h / 110.0f) + 1, k;
    float dz = h / (float)nz;
    for (k = 0; k < nz; k++)
      xf_box(f, MAT_STONE, 0.0f, 0.0f, dz * ((float)k + 0.5f), r, ry, dz * 0.5f, 0);
  }
  if (prop_lod < 1100.0f)
    xf_box(f, MAT_STONE, 0.0f, 0.0f, 12.0f, r + 7.0f, ry + 7.0f, 12.0f, 1);

  /* top slab + crenellated ring */
  xf_box(f, MAT_STONE, 0.0f, 0.0f, h + 6.0f, r + 6.0f, ry + 6.0f, 6.0f, 1);
  for (i = 0; i < sides && prop_lod < 1400.0f; i += 2) {
    float a0 = (float)i * 6.28318531f / (float)sides;
    float a1 = (float)(i + 1) * 6.28318531f / (float)sides;
    float x0 = cosf(a0) * r, y0 = sinf(a0) * ry;
    float x1 = cosf(a1) * r, y1 = sinf(a1) * ry;
    lq(f, MAT_STONE, x0, y0, h + 12.0f, x1, y1, h + 12.0f,
       x1, y1, h + 40.0f, x0, y0, h + 40.0f);
  }

  /* slate cone roof on the taller towers */
  if (p->sub != 1 && h > 240.0f) {
    float apexz = h + 120.0f;
    float rr = r + 14.0f;
    float u0, v0, u1, v1;
    cell_rect(MAT_SLATE, &u0, &v0, &u1, &v1);
    for (i = 0; i < sides; i++) {
      float a0 = (float)i * 6.28318531f / (float)sides;
      float a1 = (float)(i + 1) * 6.28318531f / (float)sides;
      V3 A = xf_pt(f, cosf(a0) * rr, sinf(a0) * rr, h + 40.0f);
      V3 B = xf_pt(f, cosf(a1) * rr, sinf(a1) * rr, h + 40.0f);
      V3 C = xf_pt(f, 0.0f, 0.0f, apexz);
      tface(MAT_SLATE, A.x, A.y, A.z, B.x, B.y, B.z, C.x, C.y, C.z,
            u0, v1, u1, v1, (u0 + u1) * 0.5f, v0);
    }
    /* banner on the spire */
    mesh_tint(210, 60, 54);
    lq(f, MAT_BANNER, rr * 0.25f, 0.0f, apexz + 4.0f, rr * 0.25f, 0.0f, apexz + 86.0f,
       rr * 0.32f, 26.0f, apexz + 74.0f, rr * 0.32f, 26.0f, apexz + 12.0f);
    mesh_tint(255, 255, 255);
  }
  ang = 0.0f; (void)ang;
}

static void draw_gate(Xf f, Prop *p)
{
  float hw = p->hw, hd = p->hd, h = p->h;
  /* two jambs and a lintel; the passage stays open */
  xf_box(f, MAT_STONE, -hw + 10.0f, 0.0f, h * 0.5f, 10.0f, hd, h * 0.5f, 0);
  xf_box(f, MAT_STONE, hw - 10.0f, 0.0f, h * 0.5f, 10.0f, hd, h * 0.5f, 0);
  xf_box(f, MAT_STONE, 0.0f, 0.0f, h + 4.0f, hw, hd + 4.0f, 14.0f, 1);
  /* portcullis bars, raised, standing proud of the arch so they catch light */
  {
    int i;
    for (i = -2; i <= 2; i++) {
      float x = (float)i * (hw * 0.32f);
      mesh_tint(104, 102, 106);
      xf_box(f, MAT_IRON, x, -hd - 5.0f, h * 0.5f, 2.5f, 2.0f, h * 0.30f, 0);
      mesh_tint(132, 128, 130);
      xf_box(f, MAT_IRON, x, -hd - 6.6f, h * 0.5f, 1.0f, 1.2f, h * 0.30f, 0);
      mesh_tint(255, 255, 255);
    }
    mesh_tint(116, 114, 118);
    xf_box(f, MAT_IRON, 0.0f, -hd - 5.0f, h * 0.18f, hw - 12.0f, 2.0f, 2.4f, 0);
    xf_box(f, MAT_IRON, 0.0f, -hd - 5.0f, h * 0.36f, hw - 12.0f, 2.0f, 2.0f, 0);
    mesh_tint(255, 255, 255);
  }
  /* dark recess behind the bars */
  mesh_tint(44, 42, 48);
  lq(f, MAT_DARK, -hw + 4.0f, -hd - 0.8f, 0.0f, hw - 4.0f, -hd - 0.8f, 0.0f,
     hw - 4.0f, -hd - 0.8f, h, -hw + 4.0f, -hd - 0.8f, h);
  mesh_tint(255, 255, 255);
  /* guttering torches on either side of the passage */
  {
    float tx = hw - 16.0f;
    float fl = 0.86f + 0.14f * sinf(game_time * 8.3f + (float)p->x * 0.02f);
    glow_billboard((float)p->x - tx, (float)p->y - (hd + 9.0f), h * 0.70f, 13.0f * fl, 255, 190, 100, 112);
    glow_billboard((float)p->x + tx, (float)p->y - (hd + 9.0f), h * 0.70f, 13.0f * fl, 255, 190, 100, 112);
    glow_billboard((float)p->x - tx, (float)p->y - (hd + 9.0f), h * 0.70f, 44.0f, 255, 138, 60, 62);
    glow_billboard((float)p->x + tx, (float)p->y - (hd + 9.0f), h * 0.70f, 44.0f, 255, 138, 60, 62);
  }
}

static void draw_house(Xf f, Prop *p)
{
  float hw = p->hw, hd = p->hd, h = p->h;
  int wall = (p->sub & 1) ? MAT_STONE : MAT_LIME;
  float ridge = h + 46.0f;
  float over = 12.0f;
  int i;

  mesh_tint(198, 194, 186);
  /* the upper storey juts out over the street */
  xf_box(f, wall, 0.0f, 0.0f, h * 0.42f, hw, hd, h * 0.42f, 0);
  xf_box(f, MAT_PLANK, 0.0f, 0.0f, h * 0.85f, hw + 6.0f, hd + 6.0f, 5.0f, 1);
  xf_box(f, wall, 0.0f, 0.0f, h * 0.85f + (h * 0.15f) * 0.5f,
         hw + 4.0f, hd + 4.0f, (h * 0.15f) * 0.5f, 0);

  /* gable roof, ridge runs along local x */
  {
    float u0, v0, u1, v1;
    cell_rect(MAT_SLATE, &u0, &v0, &u1, &v1);
    mesh_tint(255, 255, 255);
    /* south slope */
    lq(f, MAT_SLATE, -hw - over, hd + over, h, hw + over, hd + over, h,
       hw + over, 0.0f, ridge, -hw - over, 0.0f, ridge);
    /* north slope */
    lq(f, MAT_SLATE, hw + over, -hd - over, h, -hw - over, -hd - over, h,
       -hw - over, 0.0f, ridge, hw + over, 0.0f, ridge);
    /* gable ends */
    {
      float lu0, lv0, lu1, lv1;
      V3 A, B, C;
      cell_rect(MAT_LIME, &lu0, &lv0, &lu1, &lv1);
      A = xf_pt(f, hw, hd, h); B = xf_pt(f, hw, -hd, h); C = xf_pt(f, hw, 0.0f, ridge);
      tface(MAT_LIME, A.x, A.y, A.z, B.x, B.y, B.z, C.x, C.y, C.z,
            lu0, lv1, lu1, lv1, (lu0 + lu1) * 0.5f, lv0);
      A = xf_pt(f, -hw, -hd, h); B = xf_pt(f, -hw, hd, h); C = xf_pt(f, -hw, 0.0f, ridge);
      tface(MAT_LIME, A.x, A.y, A.z, B.x, B.y, B.z, C.x, C.y, C.z,
            lu1, lv1, lu0, lv1, (lu0 + lu1) * 0.5f, lv0);
    }
    (void)i;
  }

  /* door and lit windows on the street side (+y) */
  if (prop_lod > 1100.0f) { mesh_tint(255, 255, 255); return; }
  mesh_tint(64, 48, 36);
  lq(f, MAT_PLANK, -14.0f, hd + 3.0f, 0.0f, 14.0f, hd + 3.0f, 0.0f,
     14.0f, hd + 3.0f, 44.0f, -14.0f, hd + 3.0f, 44.0f);
  mesh_tint(255, 206, 130);
  lq(f, MAT_GOLD, -hw * 0.7f, hd + 3.0f, h * 0.42f, -hw * 0.4f, hd + 3.0f, h * 0.42f,
     -hw * 0.4f, hd + 3.0f, h * 0.42f + 20.0f, -hw * 0.7f, hd + 3.0f, h * 0.42f + 20.0f);
  lq(f, MAT_GOLD, hw * 0.4f, hd + 3.0f, h * 0.42f, hw * 0.7f, hd + 3.0f, h * 0.42f,
     hw * 0.7f, hd + 3.0f, h * 0.42f + 20.0f, hw * 0.4f, hd + 3.0f, h * 0.42f + 20.0f);
  mesh_tint(255, 190, 110);
  lq(f, MAT_GOLD, -hw * 0.55f, hd + 8.0f, h * 0.80f, -hw * 0.25f, hd + 8.0f, h * 0.80f,
     -hw * 0.25f, hd + 8.0f, h * 0.80f + 16.0f, -hw * 0.55f, hd + 8.0f, h * 0.80f + 16.0f);
  mesh_tint(255, 255, 255);
  {
    V3 A = xf_pt(f, -hw * 0.55f, hd + 3.0f, h * 0.42f + 10.0f);
    V3 B = xf_pt(f, hw * 0.55f, hd + 3.0f, h * 0.42f + 10.0f);
    glow_billboard(A.x, A.y, A.z, 26.0f, 255, 168, 86, 58);
    glow_billboard(B.x, B.y, B.z, 26.0f, 255, 168, 86, 58);
  }
}

static void draw_brazier(Xf f, Prop *p)
{
  float h = (float)p->h;
  float fl = 0.86f + 0.14f * sinf(game_time * 9.1f + (float)p->x * 0.01f);
  float f2 = 0.86f + 0.14f * sinf(game_time * 13.7f + (float)p->y * 0.013f);
  int i;

  /* four iron legs, a deep fire basket and a bed of embers */
  mesh_tint(118, 116, 122);
  for (i = 0; i < 4; i++) {
    float lx = (i & 1) ? 8.0f : -8.0f;
    float ly = (i & 2) ? 8.0f : -8.0f;
    xf_box(f, MAT_IRON, lx, ly, h * 0.31f, 2.6f, 2.6f, h * 0.31f, 0);
  }
  mesh_tint(94, 92, 98);
  xf_box(f, MAT_IRON, 0.0f, 0.0f, h * 0.62f + 3.0f, 8.0f, 8.0f, 6.0f, 1);
  mesh_tint(126, 124, 130);
  xf_box(f, MAT_IRON, 0.0f, 0.0f, h * 0.62f + 9.5f, 9.6f, 9.6f, 1.2f, 0);
  mesh_tint(250, 128, 48);
  xf_box(f, MAT_GOLD, 0.0f, 0.0f, h * 0.62f + 9.0f, 7.4f, 7.4f, 1.2f, 1);
  mesh_tint(255, 255, 255);

  /* flame tongues + a halo that stays small, a warm pool on the flagstones */
  glow_billboard((float)p->x, (float)p->y + 1.0f, h * 0.62f + 16.0f,
                  8.0f * fl, 255, 210, 130, 120);
  glow_billboard((float)p->x, (float)p->y + 1.0f, h * 0.62f + 20.0f,
                 19.0f * fl, 255, 176, 82, 104);
  glow_billboard((float)p->x, (float)p->y + 2.0f, h * 0.62f + 26.0f,
                 10.0f * f2, 255, 228, 166, 118);
  light_pool((float)p->x, (float)p->y, 48.0f, 255, 152, 68, 34);
  if (prop_lod < 800.0f) shadow_blob((float)p->x, (float)p->y, 22.0f, 18.0f, 60);
}

static void draw_post(Xf f, Prop *p)
{
  float h = (float)p->h;
  float fl = 0.86f + 0.14f * sinf(game_time * 10.3f + (float)p->y * 0.02f);
  mesh_tint(104, 98, 92);
  xf_box(f, MAT_IRON, 0.0f, 0.0f, h * 0.5f, 4.6f, 4.6f, h * 0.5f, 0);
  mesh_tint(255, 255, 255);
  xf_box(f, MAT_IRON, 0.0f, 0.0f, h + 4.0f, 7.0f, 7.0f, 8.0f, 1);
  glow_billboard((float)p->x, (float)p->y, h + 16.0f + (float)p->z, 12.0f * fl, 255, 204, 120, 116);
  glow_billboard((float)p->x, (float)p->y, h + 18.0f + (float)p->z, 32.0f, 255, 148, 62, 52);
}

static void draw_barrel(Xf f, Prop *p)
{
  float h = (float)p->h;
  xf_box(f, MAT_PLANK, 0.0f, 0.0f, h * 0.5f, (float)p->hw, (float)p->hd, h * 0.5f, 0);
  xf_box(f, MAT_IRON, 0.0f, 0.0f, h * 0.22f, (float)p->hw + 1.5f, (float)p->hd + 1.5f, 2.5f, 0);
  xf_box(f, MAT_IRON, 0.0f, 0.0f, h * 0.78f, (float)p->hw + 1.5f, (float)p->hd + 1.5f, 2.5f, 0);
  if (prop_lod < 800.0f) shadow_blob((float)p->x, (float)p->y, (float)p->hw + 8.0f, (float)p->hd + 8.0f, 46);
}

static void draw_crate(Xf f, Prop *p)
{
  float h = (float)p->h;
  xf_box(f, MAT_PLANK, 0.0f, 0.0f, h * 0.5f, (float)p->hw, (float)p->hd, h * 0.5f, 1);
  mesh_tint(190, 180, 160);
  lq(f, MAT_PLANK, -(float)p->hw - 0.5f, 0.0f, h * 0.5f,
     (float)p->hw + 0.5f, 0.0f, h * 0.5f,
     (float)p->hw + 0.5f, 0.0f, h * 0.5f, -(float)p->hw - 0.5f, 0.0f, h * 0.5f);
  mesh_tint(255, 255, 255);
  if (prop_lod < 800.0f) shadow_blob((float)p->x, (float)p->y, (float)p->hw + 8.0f, (float)p->hd + 8.0f, 46);
}

static void draw_banner(Xf f, Prop *p)
{
  float h = (float)p->h;
  float side = (p->sub == 1) ? -1.0f : 1.0f;
  xf_box(f, MAT_PLANK, 0.0f, 0.0f, h * 0.5f, 3.0f, 3.0f, h * 0.5f, 0);
  /* cloth with a slow billow */
  {
    float w = (float)p->hd * 2.0f + 26.0f;
    float sway = sinf(game_time * 1.6f + (float)p->x * 0.01f) * 3.0f;
    float u0, v0, u1, v1;
    cell_rect(MAT_BANNER, &u0, &v0, &u1, &v1);
    mesh_tint(220, 220, 220);
    lq(f, MAT_BANNER, -w * 0.5f, 1.0f, h, w * 0.5f, 1.0f, h,
       w * 0.5f + sway, 1.0f, h - 74.0f, -w * 0.5f + sway, 1.0f, h - 74.0f);
    if (p->sub == 1) {
      lq(f, MAT_BANNER, -w * 0.5f, -1.0f, h, w * 0.5f, -1.0f, h,
         w * 0.5f + sway, -1.0f, h - 74.0f, -w * 0.5f + sway, -1.0f, h - 74.0f);
    }
    mesh_tint(255, 255, 255);
  }
  (void)side;
}

static void draw_statue(Xf f, Prop *p)
{
  float h = (float)p->h;
  float hw = (float)p->hw, hd = (float)p->hd;
  float u0, v0, u1, v1;
  float base_h = 12.0f, ped_top, fig0;

  if (hw > 34.0f) hw = 34.0f;
  if (hd > 34.0f) hd = 34.0f;
  ped_top = h * 0.34f;
  fig0 = ped_top;

  /* stepped base, then a tapered plinth: the taper is what stops a pedestal
     looking like a crate */
  mesh_tint(126, 121, 112);
  xf_box(f, MAT_STONE, 0.0f, 0.0f, base_h * 0.5f, hw + 8.0f, hd + 8.0f, base_h * 0.5f, 1);
  mesh_tint(138, 132, 122);
  xf_box(f, MAT_STONE, 0.0f, 0.0f, base_h + 3.0f, hw + 3.0f, hd + 3.0f, 3.0f, 1);
  {
    float a = hw - 5.0f, b = hd - 5.0f, c = hw - 10.0f, d = hd - 10.0f;
    float z0 = base_h + 6.0f, z1 = ped_top - 4.0f;
    lq(f, MAT_STONE, -a, -b, z0, a, -b, z0, c * 0.68f, -d * 0.68f, z1, -c * 0.68f, -d * 0.68f, z1);
    lq(f, MAT_STONE, a,  b, z0, -a,  b, z0, -c * 0.68f,  d * 0.68f, z1, c * 0.68f,  d * 0.68f, z1);
    lq(f, MAT_STONE, -a,  b, z0, -a, -b, z0, -c * 0.68f, -d * 0.68f, z1, -c * 0.68f,  d * 0.68f, z1);
    lq(f, MAT_STONE,  a, -b, z0,  a,  b, z0,  c * 0.68f,  d * 0.68f, z1,  c * 0.68f, -d * 0.68f, z1);
  }
  mesh_tint(146, 140, 130);
  xf_box(f, MAT_STONE, 0.0f, 0.0f, ped_top - 2.0f, hw - 12.0f, hd - 12.0f, 2.0f, 1);
  mesh_tint(255, 255, 255);

  /* the figure, in weathered grey stone so the brick joints of the material
     read as tooled blocks rather than a wall */
  mesh_tint(150, 148, 143);
  xf_box(f, MAT_SLATE, -6.0f, 0.0f, fig0 + 15.0f, 4.8f, 5.4f, 15.0f, 0);
  xf_box(f, MAT_SLATE,  6.0f, 0.0f, fig0 + 15.0f, 4.8f, 5.4f, 15.0f, 0);
  /* tabard hangs to the knees, front and back */
  mesh_tint(148, 56, 54);
  xf_box(f, MAT_BANNER, 0.0f, 0.0f, fig0 + 27.0f, 10.5f, 6.6f, 13.0f, 0);
  mesh_tint(150, 148, 143);
  xf_box(f, MAT_SLATE, 0.0f, 0.0f, fig0 + 45.0f, 12.0f, 7.2f, 11.0f, 0);
  xf_box(f, MAT_SLATE, 0.0f, 0.0f, fig0 + 58.0f, 15.0f, 7.8f, 3.0f, 1);
  /* pauldrons and a helm: the silhouette does the work at this size */
  xf_box(f, MAT_SLATE, -14.0f, 0.0f, fig0 + 55.0f, 4.2f, 6.2f, 4.6f, 1);
  xf_box(f, MAT_SLATE,  14.0f, 0.0f, fig0 + 55.0f, 4.2f, 6.2f, 4.6f, 1);
  xf_box(f, MAT_SLATE, 0.0f, 0.0f, fig0 + 67.0f, 6.2f, 6.4f, 5.6f, 1);
  mesh_tint(58, 56, 60);
  xf_box(f, MAT_DARK, 0.0f, -5.6f, fig0 + 66.5f, 4.6f, 0.9f, 1.2f, 0);
  mesh_tint(176, 148, 92);
  xf_box(f, MAT_GOLD, 0.0f, 0.0f, fig0 + 71.8f, 6.4f, 6.6f, 1.0f, 0);
  mesh_tint(255, 255, 255);

  /* left arm hangs, right arm is raised with the sword point to the sky */
  mesh_tint(146, 144, 139);
  xf_box(f, MAT_SLATE, -17.0f, 0.0f, fig0 + 42.0f, 3.6f, 4.6f, 13.0f, 0);
  xf_box(f, MAT_SLATE,  17.5f, -2.0f, fig0 + 56.0f, 3.6f, 4.6f, 15.0f, 0);
  mesh_tint(206, 214, 228);
  lq(f, MAT_IRON, 19.5f, -3.0f, fig0 + 71.0f, 23.0f, -3.0f, fig0 + 71.0f,
     23.0f, -3.0f, fig0 + 104.0f, 19.5f, -3.0f, fig0 + 104.0f);
  mesh_tint(190, 160, 96);
  xf_box(f, MAT_GOLD, 21.2f, -3.0f, fig0 + 71.0f, 6.6f, 1.6f, 1.8f, 0);
  xf_box(f, MAT_GOLD, 21.2f, -3.0f, fig0 + 106.0f, 1.2f, 1.2f, 2.4f, 0);
  mesh_tint(255, 255, 255);

  /* cloak falling off the shoulders */
  cell_rect(MAT_DARK, &u0, &v0, &u1, &v1);
  tface(MAT_DARK, -15.0f, 8.6f, fig0 + 58.0f, 15.0f, 8.6f, fig0 + 58.0f,
        2.0f, 10.4f, fig0 + 14.0f, u0, v0, u1, v0, u0, v1);

  if (prop_lod < 900.0f) shadow_blob((float)p->x, (float)p->y, hw + 10.0f, hd + 10.0f, 56);
}

static void draw_cart(Xf f, Prop *p)
{
  float h = (float)p->h;
  int i;
  xf_box(f, MAT_PLANK, 0.0f, 0.0f, h * 0.62f, (float)p->hw, (float)p->hd, 12.0f, 1);
  xf_box(f, MAT_PLANK, 0.0f, 0.0f, h * 0.62f + 14.0f, (float)p->hw - 4.0f, (float)p->hd - 4.0f, 3.0f, 0);
  for (i = -1; i <= 1; i += 2) {
    float wx = (float)i * ((float)p->hw - 6.0f);
    lq(f, MAT_PLANK, wx, (float)p->hd + 2.0f, h * 0.18f, wx, (float)p->hd + 2.0f, h * 0.90f,
       wx, (float)p->hd + 3.0f, h * 0.90f, wx, (float)p->hd + 3.0f, h * 0.18f);
  }
  if (prop_lod < 800.0f) shadow_blob((float)p->x, (float)p->y, (float)p->hw + 12.0f, (float)p->hd + 12.0f, 48);
}

static void draw_boat(Xf f, Prop *p)
{
  float hw = (float)p->hw, hd = (float)p->hd, h = (float)p->h;
  float z = (float)p->z;
  /* hull sides, raked */
  mesh_tint(150, 150, 155);
  lq(f, MAT_PLANK, -hw, hd, z, hw, hd, z, hw * 0.8f, hd * 0.7f, z + h, -hw * 0.8f, hd * 0.7f, z + h);
  lq(f, MAT_PLANK, hw, -hd, z, -hw, -hd, z, -hw * 0.8f, -hd * 0.7f, z + h, hw * 0.8f, -hd * 0.7f, z + h);
  lq(f, MAT_PLANK, hw, hd, z, hw, -hd, z, hw * 0.8f, -hd * 0.7f, z + h, hw * 0.8f, hd * 0.7f, z + h);
  lq(f, MAT_PLANK, -hw, -hd, z, -hw, hd, z, -hw * 0.8f, hd * 0.7f, z + h, -hw * 0.8f, -hd * 0.7f, z + h);
  xf_box(f, MAT_PLANK, 0.0f, 0.0f, z + h, hw * 0.8f, hd * 0.7f, 3.0f, 0);
  mesh_tint(255, 255, 255);
  /* mast and a furled sail */
  xf_box(f, MAT_PLANK, 0.0f, 0.0f, z + h + 62.0f, 3.0f, 3.0f, 60.0f, 0);
  mesh_tint(190, 186, 170);
  lq(f, MAT_LIME, -26.0f, 0.0f, z + h + 40.0f, 26.0f, 0.0f, z + h + 40.0f,
     26.0f, 0.0f, z + h + 100.0f, -26.0f, 0.0f, z + h + 100.0f);
  mesh_tint(255, 255, 255);
  if (prop_lod < 800.0f) shadow_blob((float)p->x, (float)p->y, hw + 10.0f, hd + 10.0f, 40);
}

static void draw_pier(Xf f, Prop *p)
{
  float hw = (float)p->hw, hd = (float)p->hd;
  int i;
  xf_box(f, MAT_PLANK, 0.0f, 0.0f, 4.0f, hw, hd, 4.0f, 1);
  for (i = -1; i <= 1; i += 2) {
    xf_box(f, MAT_PLANK, (float)i * (hw - 8.0f), -hd + 20.0f, -4.0f, 8.0f, 8.0f, 22.0f, 0);
    xf_box(f, MAT_PLANK, (float)i * (hw - 8.0f), hd - 20.0f, -4.0f, 8.0f, 8.0f, 22.0f, 0);
  }
}

void world_draw_props(void)
{
  int i;
  for (i = 0; i < prop_count; i++) {
    Prop *p = &props[i];
    Xf f;
    float sx, sy, dep;

    if (!proj((float)p->x, (float)p->y, (float)p->z + p->h * 0.5f, &sx, &sy, &dep))
      continue;
    if (dep > 2450.0f) continue;
    if (sx < -260.0f || sx > SCR_W + 260.0f) continue;

    prop_lod = dep;
    f = xf_move(xf_id(), (float)p->x, (float)p->y, (float)p->z);
    if (p->type == P_BANNER) f = xf_roty(f, 0.0f);

    switch (p->type) {
      case P_WALL:   draw_wall(f, p); break;
      case P_TOWER:  draw_tower(f, p); break;
      case P_GATE:   draw_gate(f, p); break;
      case P_HOUSE:  draw_house(f, p); break;
      case P_POST:   draw_post(f, p); break;
      case P_BRAZIER:draw_brazier(f, p); break;
      case P_BARREL: draw_barrel(f, p); break;
      case P_CRATE:  draw_crate(f, p); break;
      case P_BANNER: draw_banner(f, p); break;
      case P_STATUE: draw_statue(f, p); break;
      case P_CART:   draw_cart(f, p); break;
      case P_BOAT:   draw_boat(f, p); break;
      case P_PIER:   draw_pier(f, p); break;
      default: break;
    }
  }
}
