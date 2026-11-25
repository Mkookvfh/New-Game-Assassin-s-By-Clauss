/* THE ASHEN KNIGHT — camera, input, combat, HUD. */
#include <math.h>
#include <string.h>
#include "game.h"

Cam cam;
Player player;
Enemy enemies[MAX_ENEMIES];
int enemy_count;
float game_time;

static int started, paused;
static int lock_target;
static int combo_queued;
static float zone_name_t;
static int zone_id;

/* ------------------------------------------------------------------ camera */

static float cfx, cfy, cfz, crx, cry, crz, cux, cuy, cuz;

static void camera_basis(void)
{
  float cy = cosf(cam.yaw), sy = sinf(cam.yaw);
  float cp = cosf(cam.pitch), sp = sinf(cam.pitch);
  cfx = sy * cp; cfy = -cy * cp; cfz = -sp;
  crx = cy; cry = sy; crz = 0.0f;
  cux = sp * sy; cuy = -sp * cy; cuz = cp;
  cam_fwd_x = cfx; cam_fwd_y = cfy; cam_fwd_z = cfz;
  cam_rgt_x = crx; cam_rgt_y = cry; cam_rgt_z = crz;
  cam_up_x = cux; cam_up_y = cuy; cam_up_z = cuz;
}

int proj(float wx, float wy, float wz, float *sx, float *sy, float *dep)
{
  float dx = wx - cam.x, dy = wy - cam.y, dz = wz - cam.z;
  float vz = dx * cfx + dy * cfy + dz * cfz;
  float vx, vy;
  if (vz < 8.0f) return 0;
  vx = dx * crx + dy * cry;
  vy = dx * cux + dy * cuy + dz * cuz;
  *sx = SCR_W * 0.5f + vx * cam.focal / vz;
  *sy = SCR_H * 0.5f + 22.0f - vy * cam.focal / vz;
  *dep = vz;
  return 1;
}

void project_3d(float wx, float wy, float wz, float *sx, float *sy)
{
  float d;
  if (!proj(wx, wy, wz, sx, sy, &d)) { *sx = -9999.0f; *sy = -9999.0f; }
}

/* ------------------------------------------------------------------- fx */

typedef struct {
  float x, y, z, vx, vy, vz, life, max, size;
  int kind, r, g, b;          /* 0 spark, 1 ember, 2 dust */
} Spark;
#define MAX_SPARKS 120
static Spark sparks[MAX_SPARKS];

static unsigned int srand_state = 12345u;
static float frand(void)
{
  srand_state = srand_state * 1664525u + 1013904223u;
  return (float)((srand_state >> 8) & 0xffffu) / 65535.0f;
}

static Spark *spark_alloc(void)
{
  int i;
  for (i = 0; i < MAX_SPARKS; i++)
    if (sparks[i].life <= 0.0f) return &sparks[i];
  return 0;
}

static void spark_burst(float x, float y, float z, float spread, int n,
                        int r, int g, int b)
{
  int i;
  for (i = 0; i < n; i++) {
    Spark *s = spark_alloc();
    if (!s) return;
    s->x = x; s->y = y; s->z = z;
    s->vx = (frand() - 0.5f) * spread;
    s->vy = (frand() - 0.5f) * spread;
    s->vz = frand() * spread * 1.2f + 26.0f;
    s->max = s->life = 0.26f + frand() * 0.24f;
    s->size = 6.0f;
    s->kind = 0;
    s->r = r; s->g = g; s->b = b;
  }
}

/* a single ember lifting off a fire */
static void ember_emit(float x, float y, float z)
{
  Spark *s = spark_alloc();
  if (!s) return;
  s->x = x + (frand() - 0.5f) * 12.0f;
  s->y = y + (frand() - 0.5f) * 12.0f;
  s->z = z;
  s->vx = (frand() - 0.5f) * 16.0f;
  s->vy = (frand() - 0.5f) * 16.0f;
  s->vz = 12.0f + frand() * 26.0f;
  s->max = s->life = 0.85f + frand() * 0.75f;
  s->size = 2.4f + frand() * 2.4f;
  s->kind = 1;
  s->r = 255; s->g = 168; s->b = 84;
}

/* kicked-up dust, used for footfalls and rolls */
static void dust_emit(float x, float y, float z)
{
  Spark *s = spark_alloc();
  if (!s) return;
  s->x = x + (frand() - 0.5f) * 14.0f;
  s->y = y + (frand() - 0.5f) * 14.0f;
  s->z = z + frand() * 6.0f;
  s->vx = (frand() - 0.5f) * 26.0f;
  s->vy = (frand() - 0.5f) * 26.0f;
  s->vz = 4.0f + frand() * 10.0f;
  s->max = s->life = 0.45f + frand() * 0.3f;
  s->size = 7.0f + frand() * 6.0f;
  s->kind = 2;
  s->r = 150; s->g = 148; s->b = 142;
}

static void sparks_update(float dt)
{
  int i;
  for (i = 0; i < MAX_SPARKS; i++) {
    Spark *s = &sparks[i];
    if (s->life <= 0.0f) continue;
    s->life -= dt;
    s->x += s->vx * dt;
    s->y += s->vy * dt;
    s->z += s->vz * dt;
    if (s->kind == 0) {
      s->vz -= 300.0f * dt;
      if (s->z < 2.0f) { s->z = 2.0f; s->vz *= -0.35f; }
    } else if (s->kind == 1) {
      s->vz += 10.0f * dt;                    /* embers rise as they cool */
      s->vx *= (1.0f - 0.9f * dt);
      s->vy *= (1.0f - 0.9f * dt);
    } else {
      s->vx *= (1.0f - 3.2f * dt);
      s->vy *= (1.0f - 3.2f * dt);
      s->vz *= (1.0f - 1.8f * dt);
    }
  }
}

static void sparks_draw(void)
{
  int i;
  for (i = 0; i < MAX_SPARKS; i++) {
    Spark *s = &sparks[i];
    float t, sz;
    int al;
    if (s->life <= 0.0f) continue;
    t = s->life / s->max;
    if (t < 0.0f) t = 0.0f;
    if (s->kind == 0) {
      sz = s->size + 8.0f * t;
      al = (int)(110.0f * t);
    } else if (s->kind == 1) {
      sz = s->size * (0.7f + 0.6f * t);
      al = (int)(130.0f * t * t + 14.0f * t);
    } else {
      sz = s->size * (1.0f + 1.8f * (1.0f - t));
      al = (int)(46.0f * t);
    }
    if (al < 2) continue;
    glow_billboard(s->x, s->y, s->z, sz, s->r, s->g, s->b, al);
  }
}

/* --------------------------------------------------------------- helpers */

static float dist2(float ax, float ay, float bx, float by)
{
  float dx = ax - bx, dy = ay - by;
  return dx * dx + dy * dy;
}

static float approach_angle(float a, float b, float rate, float dt)
{
  float d = wrap_pi(b - a);
  float m = rate * dt;
  if (d > m) d = m;
  if (d < -m) d = -m;
  return a + d;
}

static int blocked(float x, float y)
{
  return tile_solid_at(x, y) || prop_blocks(x, y, 11.0f);
}

/* ------------------------------------------------------------------- init */

static const float spawns[][2] = {
  { 980.0f, 900.0f }, { 700.0f, 1000.0f },
  { 860.0f, 1620.0f }, { 900.0f, 1880.0f },
  { 900.0f, 2400.0f }, { 700.0f, 2650.0f }, { 1100.0f, 2900.0f },
  { 896.0f, 3080.0f },
  { 900.0f, 3420.0f }
};

void game_init(void)
{
  int i, n = (int)(sizeof(spawns) / sizeof(spawns[0]));

  world_gen();

  memset(&player, 0, sizeof(player));
  player.x = 896.0f;
  player.y = 1080.0f;
  player.yaw = 3.14159265f;
  player.hp = player.maxhp = 100.0f;
  player.sta = 100.0f;
  player.potions = 3.0f;

  cam.yaw = 3.14159265f;
  cam.pitch = 0.15f;
  cam.dist = 215.0f;
  cam.focal = 400.0f;

  enemy_count = n;
  for (i = 0; i < n; i++) {
    Enemy *e = &enemies[i];
    memset(e, 0, sizeof(*e));
    e->x = spawns[i][0];
    e->y = spawns[i][1];
    e->yaw = frand() * 6.28f;
    e->type = (i == 7) ? 1 : 0;
    e->maxhp = e->hp = e->type ? 240.0f : 66.0f;
    e->alive = 1;
    e->state = 0;
  }

  started = 0;
  paused = 0;
  lock_target = -1;
  player.lock = 0;
  game_time = 0.0f;
  zone_id = -1;
  zone_name_t = 0.0f;
  memset(sparks, 0, sizeof(sparks));
}

int game_started(void) { return started; }

/* ------------------------------------------------------------------- zone */

static void zone_update(float dt)
{
  static const float fog_c[4][3] = {
    { 0.095f, 0.105f, 0.145f },   /* courtyard */
    { 0.060f, 0.075f, 0.120f },   /* bridge */
    { 0.120f, 0.090f, 0.080f },   /* town */
    { 0.070f, 0.090f, 0.110f }    /* harbour */
  };
  float t;
  int id;

  if (player.y < 1400.0f) id = 0;
  else if (player.y < 2100.0f) id = 1;
  else if (player.y < 3260.0f) id = 2;
  else id = 3;

  if (id != zone_id) {
    zone_id = id;
    zone_name_t = 3.4f;
  }
  t = 1.0f - expf(-dt * 1.6f);
  fog_r += (fog_c[id][0] - fog_r) * t;
  fog_g += (fog_c[id][1] - fog_g) * t;
  fog_b += (fog_c[id][2] - fog_b) * t;
  if (zone_name_t > 0.0f) zone_name_t -= dt;
}

/* -------------------------------------------------------------- sky / hud */

static float hash01(int i)
{
  unsigned int h = (unsigned int)i * 2654435761u + 0x9e3779b9u;
  h ^= h >> 15;
  h *= 0x85ebca6bu;
  h ^= h >> 13;
  return (float)(h & 0xffffu) / 65535.0f;
}

/* additive soft ellipse in screen space, used for halos, clouds and haze */
static void sky_blob(float cx, float cy, float rx, float ry,
                     int r, int g, int b, int a)
{
  float u[4] = { 0.02f, 0.98f, 0.98f, 0.02f };
  float v[4] = { 0.02f, 0.02f, 0.98f, 0.98f };
  float ax[4], ay[4];
  if (cx + rx < 0.0f || cx - rx > SCR_W) return;
  ax[0] = cx - rx; ay[0] = cy - ry;
  ax[1] = cx + rx; ay[1] = cy - ry;
  ax[2] = cx + rx; ay[2] = cy + ry;
  ax[3] = cx - rx; ay[3] = cy + ry;
  plat_tri(ax, ay, u, v, 4, 2, 0, r, g, b, a, 1, 0);
}

/* a round disc built from horizontal strips: the moon and any other round
 * light in the sky.  A plain rect reads as a white box on hardware. */
static void sky_disc(float cx, float cy, float r, int cr, int cg, int cb, int ca)
{
  int i;
  const int rows = 10;
  for (i = 0; i < rows; i++) {
    float t0 = -1.0f + 2.0f * (float)i / (float)rows;
    float t1 = -1.0f + 2.0f * (float)(i + 1) / (float)rows;
    float w0 = sqrtf(1.0f - t0 * t0) * r;
    float w1 = sqrtf(1.0f - t1 * t1) * r;
    float w = (w0 > w1 ? w0 : w1);
    float y = cy + t0 * r;
    float h = (t1 - t0) * r + 1.0f;
    plat_rect(cx - w, y, w * 2.0f, h, cr, cg, cb, ca);
  }
}

/* pin a direction in the sky and return its screen position */
static int sky_point(float dx, float dy, float dz, float *sx, float *sy)
{
  float len = sqrtf(dx * dx + dy * dy + dz * dz) + 0.0001f;
  float px = cam.x + dx / len * 5000.0f;
  float py = cam.y + dy / len * 5000.0f;
  float pz = cam.z + dz / len * 5000.0f;
  float dep;
  return proj(px, py, pz, sx, sy, &dep);
}

static void draw_sky(void)
{
  float horizon = SCR_H * 0.5f + 22.0f - tanf(cam.pitch) * cam.focal;
  int i, bands = 18;
  float sx, sy;

  if (horizon > SCR_H) horizon = SCR_H;
  if (horizon < 40.0f) horizon = 40.0f;

  /* vertical gradient: near-black at the zenith down to the zone fog colour */
  for (i = 0; i < bands; i++) {
    float t0 = (float)i / (float)bands;
    float t1 = (float)(i + 1) / (float)bands;
    float tf = powf(t1, 0.65f);
    int r = (int)(9.0f + (fog_r * 255.0f * 1.45f - 9.0f) * tf);
    int g = (int)(11.0f + (fog_g * 255.0f * 1.45f - 11.0f) * tf);
    int b = (int)(21.0f + (fog_b * 255.0f * 1.55f - 21.0f) * tf);
    plat_rect(0.0f, horizon * t0, SCR_W, horizon * (t1 - t0) + 1.0f, r, g, b, 128);
  }

  /* atmospheric perspective: a wide haze band sitting on the horizon so the
     far geometry melts into the sky instead of ending on a hard line */
  sky_blob(SCR_W * 0.5f, horizon, SCR_W * 0.95f, 96.0f,
           (int)(fog_r * 255.0f * 1.6f) + 14, (int)(fog_g * 255.0f * 1.5f) + 12,
           (int)(fog_b * 255.0f * 1.7f) + 16, 34);
  sky_blob(SCR_W * 0.35f, horizon + 12.0f, SCR_W * 0.5f, 54.0f, 96, 92, 128, 16);

  /* slow drifting cloud banks: stretched additive blobs, one per layer */
  for (i = 0; i < 4; i++) {
    float az = hash01(i * 31 + 7) * 6.28318531f + game_time * 0.006f;
    float el = 0.34f + hash01(i * 17 + 3) * 0.55f;
    float dx = cosf(az) * cosf(el), dy = sinf(az) * cosf(el), dz = sinf(el);
    float w = 190.0f + hash01(i * 5 + 11) * 210.0f;
    float hgt = 22.0f + hash01(i * 9 + 2) * 22.0f;
    if (!sky_point(dx, dy, dz, &sx, &sy)) continue;
    if (sy > horizon + 30.0f) continue;
    sky_blob(sx, sy, w, hgt, 74, 80, 104, 15);
    sky_blob(sx + w * 0.25f, sy + hgt * 0.35f, w * 0.55f, hgt * 0.7f, 96, 92, 110, 11);
  }

  /* stars: fixed directions, gentle twinkle, brighter ones a touch larger */
  for (i = 0; i < 80; i++) {
    float az = hash01(i * 7 + 1) * 6.28318531f;
    float el = 0.10f + hash01(i * 13 + 5) * 1.25f;
    float tw = 0.55f + 0.45f * sinf(game_time * (1.1f + hash01(i * 3) * 2.2f) + i);
    float big = hash01(i * 23 + 9);
    float dx = cosf(az) * cosf(el), dy = sinf(az) * cosf(el), dz = sinf(el);
    if (!sky_point(dx, dy, dz, &sx, &sy)) continue;
    if (sy > horizon - 6.0f) continue;
    if (sx < -4.0f || sx > SCR_W + 4.0f) continue;
    if (big > 0.88f) {
      sky_blob(sx, sy, 6.0f, 6.0f, 200, 210, 236, (int)(26.0f * tw));
      plat_rect(sx - 1.0f, sy - 1.0f, 3.0f, 3.0f, 226, 232, 246, (int)(110.0f * tw));
    } else {
      plat_rect(sx, sy, 2.0f, 2.0f, 186, 198, 224, (int)(70.0f * tw));
    }
  }

  /* moon, hung in the north-west so the approach shots always frame it */
  if (sky_point(-0.30f, -0.90f, 0.34f, &sx, &sy)) {
    sky_blob(sx, sy, 210.0f, 210.0f, 120, 136, 176, 16);
    sky_blob(sx, sy, 132.0f, 132.0f, 176, 190, 226, 22);
    sky_blob(sx, sy, 68.0f, 68.0f, 210, 220, 240, 34);
    sky_blob(sx, sy, 34.0f, 34.0f, 238, 240, 246, 52);
    sky_disc(sx, sy, 13.0f, 228, 228, 214, 108);
    sky_disc(sx, sy, 11.0f, 244, 242, 228, 126);
    sky_disc(sx - 3.5f, sy - 3.0f, 5.0f, 250, 248, 238, 96);
  }
}

static void bar(float x, float y, float w, float h, float frac,
                int r, int g, int b, int tr, int tg, int tb)
{
  plat_rect(x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f, 10, 10, 12, 110);
  plat_rect(x, y, w, h, tr, tg, tb, 60);
  if (frac > 0.0f) plat_rect(x, y, w * frac, h, r, g, b, 128);
}

static void num(char *b, int v)
{
  char t[8];
  int n = 0, i;
  if (v <= 0) { b[0] = '0'; b[1] = 0; return; }
  while (v > 0) { t[n++] = (char)('0' + v % 10); v /= 10; }
  for (i = 0; i < n; i++) b[i] = t[n - 1 - i];
  b[n] = 0;
}

static void draw_hud(void)
{
  char buf[32];
  int i;

  if (!started) {
    const char *t1 = "THE ASHEN KNIGHT";
    const char *t2 = "PRESS START";
    const char *t3 = "AN ACCOUNT OF ASH, IRON AND OLD OATHS";
    const char *t4 = "STICK MOVE   X ATTACK   O ROLL   L1 LOCK   TRIANGLE VIAL";
    float w, pulse = 0.60f + 0.40f * sinf(game_time * 2.2f);
    int k;

    /* soft night vignette */
    for (k = 0; k < 5; k++)
      plat_rect(0.0f, (float)(k * 26), SCR_W, 26.0f, 4, 5, 8, 128 - k * 22);
    for (k = 0; k < 5; k++)
      plat_rect(0.0f, SCR_H - 130.0f + (float)(k * 26), SCR_W, 26.0f, 4, 5, 8, 24 + k * 24);
    plat_rect(0.0f, SCR_H * 0.5f - 138.0f, SCR_W, 168.0f, 5, 5, 9, 76);

    w = text_width(t1, 3.0f);
    plat_text(SCR_W * 0.5f - w * 0.5f, SCR_H * 0.5f - 120.0f, 3.0f, t1, 238, 230, 208, 128);
    plat_rect(SCR_W * 0.5f - 150.0f, SCR_H * 0.5f - 82.0f, 300.0f, 1.5f, 176, 148, 96, 110);

    w = text_width(t3, 1.0f);
    plat_text(SCR_W * 0.5f - w * 0.5f, SCR_H * 0.5f - 72.0f, 1.0f, t3, 150, 142, 128, 118);

    w = text_width(t2, 2.0f);
    plat_text(SCR_W * 0.5f - w * 0.5f, SCR_H * 0.5f - 44.0f, 2.0f, t2, 226, 200, 150,
              (int)(40.0f + pulse * 88.0f));

    w = text_width(t4, 1.0f);
    plat_text(SCR_W * 0.5f - w * 0.5f, SCR_H - 44.0f, 1.0f, t4, 150, 146, 138, 100);
    return;
  }

  bar(22.0f, 24.0f, 190.0f, 10.0f, player.hp / player.maxhp, 168, 32, 36, 60, 20, 22);
  bar(22.0f, 42.0f, 140.0f, 6.0f, player.sta / 100.0f, 150, 150, 76, 48, 48, 26);

  plat_text(224.0f, 26.0f, 1.0f, "VIALS", 176, 168, 150, 110);
  plat_text(224.0f + text_width("VIALS", 1.0f) + 5.0f, 26.0f, 1.0f, "X", 150, 142, 128, 100);
  num(buf, (int)player.potions);
  plat_text(224.0f + text_width("VIALS X", 1.0f) + 8.0f, 26.0f, 1.0f, buf, 226, 200, 140, 128);

  /* enemy health pips over the head */
  for (i = 0; i < enemy_count; i++) {
    Enemy *e = &enemies[i];
    float sx, sy;
    if (!e->alive) continue;
    if (dist2(e->x, e->y, player.x, player.y) > 950.0f * 950.0f) continue;
    project_3d(e->x, e->y, 122.0f, &sx, &sy);
    if (sx < -40.0f || sx > SCR_W + 40.0f) continue;
    bar(sx - 26.0f, sy, 52.0f, 4.0f, e->hp / e->maxhp, 156, 44, 40, 44, 16, 16);
  }

  /* boss bar */
  for (i = 0; i < enemy_count; i++) {
    Enemy *e = &enemies[i];
    if (!e->type || !e->alive) continue;
    if (dist2(e->x, e->y, player.x, player.y) > 700.0f * 700.0f) continue;
    plat_text(SCR_W * 0.5f - 62.0f, SCR_H - 52.0f, 1.0f, "THE WARDEN", 200, 150, 130, 120);
    bar(SCR_W * 0.5f - 130.0f, SCR_H - 38.0f, 260.0f, 8.0f, e->hp / e->maxhp, 150, 40, 44, 50, 18, 20);
  }

  /* lock reticle */
  if (lock_target >= 0 && enemies[lock_target].alive) {
    Enemy *e = &enemies[lock_target];
    float sx, sy;
    project_3d(e->x, e->y, 100.0f, &sx, &sy);
    if (sx > -50.0f) {
      plat_rect(sx - 8.0f, sy - 1.0f, 16.0f, 2.0f, 230, 214, 150, 110);
      plat_rect(sx - 1.0f, sy - 8.0f, 2.0f, 16.0f, 230, 214, 150, 110);
    }
  }

  /* area banner */
  if (zone_name_t > 0.0f) {
    static const char *names[4] = { "THE COURTYARD", "THE OLD BRIDGE", "ASHEN TOWN", "THE HARBOUR" };
    float a = minf(1.0f, zone_name_t * 1.4f) * minf(1.0f, (3.4f - zone_name_t) * 3.0f);
    int al = (int)(a * 118.0f);
    const char *s = names[zone_id < 0 ? 0 : zone_id];
    float w = text_width(s, 2.0f);
    plat_text(SCR_W * 0.5f - w * 0.5f, 74.0f, 2.0f, s, 236, 228, 206, al);
    plat_rect(SCR_W * 0.5f - w * 0.5f, 96.0f, w, 1.0f, 200, 190, 168, al / 2);
  }

  if (paused && started) {
    const char *t = "PAUSED";
    float w = text_width(t, 2.0f);
    plat_rect(0.0f, 0.0f, SCR_W, SCR_H, 6, 6, 9, 110);
    plat_text(SCR_W * 0.5f - w * 0.5f, SCR_H * 0.5f - 12.0f, 2.0f, t, 230, 224, 206, 128);
  }

  if (player.dead) {
    const char *t = "YOU DIED";
    const char *t2 = "PRESS START TO RISE AGAIN";
    float w = text_width(t, 2.0f);
    plat_rect(0.0f, 0.0f, SCR_W, SCR_H, 18, 4, 5, 100);
    plat_text(SCR_W * 0.5f - w * 0.5f, SCR_H * 0.5f - 30.0f, 2.0f, t, 190, 40, 40, 128);
    w = text_width(t2, 1.0f);
    plat_text(SCR_W * 0.5f - w * 0.5f, SCR_H * 0.5f + 12.0f, 1.0f, t2, 200, 180, 160,
              (int)(70.0f + 50.0f * sinf(game_time * 2.0f)));
  }
}

/* ---------------------------------------------------------------- player */

static void player_update(float dt, const Input *in)
{
  float inx = in->mx, iny = in->my;
  float len = sqrtf(inx * inx + iny * iny);
  float dirx = 0.0f, diry = 0.0f;

  if (len > 0.18f) {
    float cy = cosf(cam.yaw), sy = sinf(cam.yaw);
    float fhx = sy, fhy = -cy;
    dirx = (crx * inx + fhx * iny) / len;
    diry = (cry * inx + fhy * iny) / len;
    inx /= len; iny /= len;
  }

  if (player.dead) {
    player.moving = 0;
    player.running = 0;
    player.dead_t += dt;
    return;
  }

  /* roll */
  if (player.rolling) {
    float spd = 330.0f;
    player.roll_t += dt;
    if (player.roll_t < 0.10f) spd *= 0.4f;
    else if (player.roll_t > 0.40f) spd *= 0.5f;
    {
      float mx = sinf(player.yaw) * spd * dt;
      float my = -cosf(player.yaw) * spd * dt;
      if (!blocked(player.x + mx, player.y)) player.x += mx;
      if (!blocked(player.x, player.y + my)) player.y += my;
    }
    if (player.roll_t > 0.52f) { player.rolling = 0; player.roll_t = 0.0f; }
    player.anim += dt * 2.0f;
    return;
  }

  if (player.attacking) {
    float dur = player.atk_stage > 0.5f ? 0.58f : 0.64f;
    float prev = player.atk_t;
    player.atk_t += dt / dur;
    if (player.atk_stage < 0.5f && prev < 0.34f && player.atk_t >= 0.34f) {
      /* the strike lands */
      int i;
      for (i = 0; i < enemy_count; i++) {
        Enemy *e = &enemies[i];
        float d, ang;
        if (!e->alive) continue;
        d = sqrtf(dist2(e->x, e->y, player.x, player.y));
        if (d > 128.0f) continue;
        ang = wrap_pi(atan2f(e->x - player.x, -(e->y - player.y)) - player.yaw);
        if (ang > 1.25f || ang < -1.25f) continue;
        e->hp -= e->type ? 30.0f : 26.0f;
        e->hurt = 0.26f;
        e->state = 5;
        e->vx = sinf(player.yaw) * 210.0f;
        e->vy = -cosf(player.yaw) * 210.0f;
        spark_burst(e->x, e->y, 70.0f, 240.0f, 7, 255, 226, 170);
        if (e->hp <= 0.0f) { e->alive = 0; e->state = 6; e->dead_t = 0.0f; }
      }
    }
    if (player.atk_t >= 1.0f) {
      if (player.atk_stage < 0.5f && combo_queued && player.sta > 10.0f) {
        player.atk_t = 0.0f;
        player.atk_stage = 1.0f;
        player.sta -= 10.0f;
      } else {
        player.attacking = 0;
        player.atk_t = 0.0f;
        player.atk_stage = 0.0f;
      }
      combo_queued = 0;
    }
    player.moving = 0;
    player.anim += dt * 3.0f;
    return;
  }

  /* walk / run */
  if (len > 0.18f) {
    float target = atan2f(dirx, -diry);
    float spd = in->run && player.sta > 1.0f ? 178.0f : 96.0f;
    player.yaw = approach_angle(player.yaw, target, 11.0f, dt);
    player.moving = 1;
    player.running = (spd > 150.0f);
    if (player.running) {
      player.sta -= 20.0f * dt;
      if (player.sta < 0.0f) player.sta = 0.0f;
    }
    {
      float wx = sinf(player.yaw) * spd * dt;
      float wy = -cosf(player.yaw) * spd * dt;
      if (!blocked(player.x + wx, player.y)) player.x += wx;
      else player.anim += dt * 4.0f;
      if (!blocked(player.x, player.y + wy)) player.y += wy;
      else player.anim += dt * 4.0f;
    }
    player.anim += spd * dt * 0.088f;
  } else {
    player.moving = 0;
    player.running = 0;
    player.anim += dt * 1.2f;
  }
  if (!player.running) {
    player.sta += 26.0f * dt;
    if (player.sta > 100.0f) player.sta = 100.0f;
  }
}

/* ---------------------------------------------------------------- enemies */

static void enemies_update(float dt)
{
  int i;

  for (i = 0; i < enemy_count; i++) {
    Enemy *e = &enemies[i];
    float d, ang;

    if (!e->alive) {
      e->dead_t += dt;
      e->x += e->vx * dt;
      e->y += e->vy * dt;
      e->vx *= 0.88f;
      e->vy *= 0.88f;
      continue;
    }
    if (e->hurt > 0.0f) e->hurt -= dt;
    if (e->x != e->x) e->x = player.x;   /* paranoia against NaN */
    d = sqrtf(dist2(e->x, e->y, player.x, player.y));
    ang = atan2f(player.x - e->x, -(player.y - e->y));

    if (e->state == 5) {
      e->x += e->vx * dt;
      e->y += e->vy * dt;
      e->vx *= 0.85f;
      e->vy *= 0.85f;
      if (e->hurt <= 0.0f) e->state = 0;
      e->anim += dt * 2.0f;
      continue;
    }

    if (player.dead) { e->state = 0; e->anim += dt * 1.0f; continue; }

    if (e->state == 0) {
      e->cd -= dt;
      e->yaw = approach_angle(e->yaw, ang, 2.4f, dt);
      if (d < 760.0f) e->state = 1;
      e->anim += dt * 1.0f;
    } else if (e->state == 1) {
      e->yaw = approach_angle(e->yaw, ang, 5.5f, dt);
      if (d > 780.0f) { e->state = 0; }
      else if (d > 104.0f) {
        float spd = (e->type ? 74.0f : 68.0f);
        float nx = e->x + (player.x - e->x) / (d + 0.001f) * spd * dt;
        float ny = e->y + (player.y - e->y) / (d + 0.001f) * spd * dt;
        if (!tile_solid_at(nx, e->y)) e->x = nx;
        if (!tile_solid_at(e->x, ny)) e->y = ny;
        e->anim += spd * dt * 0.09f;
      } else if (e->cd <= 0.0f) {
        e->state = 2;
        e->windup = 0.45f;
      }
    } else if (e->state == 2) {
      e->windup -= dt;
      e->yaw = approach_angle(e->yaw, ang, 3.0f, dt);
      if (e->windup <= 0.0f) { e->state = 3; e->strike = 0.24f; }
    } else if (e->state == 3) {
      float prev = e->strike;
      e->strike -= dt;
      if (prev > 0.15f && e->strike <= 0.15f) {
        float a2 = wrap_pi(atan2f(player.x - e->x, -(player.y - e->y)) - e->yaw);
        if (d < 132.0f && a2 < 1.1f && a2 > -1.1f && !player.rolling) {
          float dmg = e->type ? 15.0f : 9.0f;
          player.hp -= dmg;
          player.hurt_t = 0.3f;
          player.x += sinf(e->yaw) * 14.0f;
          player.y += -cosf(e->yaw) * 14.0f;
          spark_burst(player.x, player.y, 74.0f, 190.0f, 6, 220, 90, 70);
          if (player.hp <= 0.0f) { player.hp = 0.0f; player.dead = 1; player.dead_t = 0.0f; }
        }
      }
      if (e->strike <= 0.0f) { e->state = 4; e->recover = 0.62f; }
    } else if (e->state == 4) {
      e->recover -= dt;
      if (e->recover <= 0.0f) { e->state = 1; e->cd = 0.9f + frand() * 0.7f; }
    }

    /* keep the mobs apart */
    {
      int j;
      for (j = 0; j < enemy_count; j++) {
        Enemy *o = &enemies[j];
        float dx, dy, l;
        if (j == i || !o->alive) continue;
        dx = e->x - o->x; dy = e->y - o->y;
        l = dx * dx + dy * dy;
        if (l > 1.0f && l < 72.0f * 72.0f) {
          l = sqrtf(l);
          e->x += dx / l * 26.0f * dt;
          e->y += dy / l * 26.0f * dt;
        }
      }
    }
  }
}

/* ------------------------------------------------------------------ frame */

static void lock_update(float dt, const Input *in)
{
  int i;

  if (lock_target >= 0 && (!enemies[lock_target].alive ||
      dist2(enemies[lock_target].x, enemies[lock_target].y, player.x, player.y) > 1100.0f * 1100.0f))
    lock_target = -1;

  if (in->lock == 2) {
    if (lock_target >= 0) lock_target = -1;
    else {
      float best = 1e9f;
      int bi = -1;
      for (i = 0; i < enemy_count; i++) {
        float d;
        if (!enemies[i].alive) continue;
        d = dist2(enemies[i].x, enemies[i].y, player.x, player.y);
        if (d < best && d < 900.0f * 900.0f) { best = d; bi = i; }
      }
      lock_target = bi;
    }
  }
  player.lock = lock_target >= 0 ? 1 : 0;
  (void)dt;
}

static void camera_update(float dt, const Input *in)
{
  float tx = player.x, ty = player.y, tz = 64.0f;

  if (lock_target >= 0 && enemies[lock_target].alive) {
    Enemy *e = &enemies[lock_target];
    float want = atan2f(e->x - player.x, -(e->y - player.y));
    cam.yaw = approach_angle(cam.yaw, want, 3.2f, dt);
    if (!player.rolling && !player.attacking)
      player.yaw = approach_angle(player.yaw, want, 6.0f, dt);
  } else {
    if (fabsf(in->rx) > 0.2f) cam.yaw -= in->rx * 2.4f * dt;
    else if (player.moving) cam.yaw = approach_angle(cam.yaw, player.yaw, 1.5f, dt);
    else cam.yaw -= in->rx * 2.4f * dt;
  }
  if (fabsf(in->ry) > 0.2f) {
    cam.pitch += in->ry * 0.9f * dt;
    cam.pitch = clampf(cam.pitch, -0.10f, 0.85f);
  }

  camera_basis();

  cam.x = tx - cfx * cam.dist;
  cam.y = ty - cfy * cam.dist;
  cam.z = tz - cfz * cam.dist;
  if (cam.z < 26.0f) cam.z = 26.0f;
  camera_basis();
}

void game_frame(float dt, const Input *in, int force_pause)
{
  static Input prev;
  static float ember_t, dust_acc;
  float saved_pitch, saved_dist;
  Input edge;

  if (dt > 0.1f) dt = 0.1f;

  /* the title wants a wider, lower composition so the hero sits below the
     title block instead of behind it */
  saved_pitch = cam.pitch;
  saved_dist = cam.dist;
  if (!started) { cam.pitch = 0.34f; cam.dist = 252.0f; }

  edge.mx = in->mx; edge.my = in->my;
  edge.rx = in->rx; edge.ry = in->ry;
  edge.attack = (in->attack && !prev.attack) ? 1 : 0;
  edge.roll = (in->roll && !prev.roll) ? 1 : 0;
  edge.potion = (in->potion && !prev.potion) ? 1 : 0;
  edge.run = in->run;
  edge.start = (in->start && !prev.start) ? 1 : 0;
  edge.lock = (in->lock && !prev.lock) ? 1 : 0;
  edge.back = (in->back && !prev.back) ? 1 : 0;
  prev = *in;

  if (edge.start) {
    if (!started) { started = 1; combo_queued = 0; }
    else if (player.dead) { game_init(); started = 1; combo_queued = 0; }
    else paused = !paused;
  }
  if (edge.back && started) paused = !paused;

  camera_basis();

  if (!force_pause && !paused && started) {
    game_time += dt;
    player.hurt_t -= dt;
    if (player.hurt_t < 0.0f) player.hurt_t = 0.0f;

    lock_update(dt, &edge);

    if (edge.roll && !player.rolling && !player.attacking && player.sta > 16.0f && !player.dead) {
      player.rolling = 1;
      player.roll_t = 0.0f;
      player.sta -= 16.0f;
    }
    if (edge.attack && !player.rolling && !player.dead) {
      if (!player.attacking && player.sta > 6.0f) {
        player.attacking = 1;
        player.atk_t = 0.0f;
        player.atk_stage = 0.0f;
        player.sta -= 6.0f;
        combo_queued = 0;
      } else if (player.attacking && player.atk_stage < 0.5f && player.atk_t > 0.45f) {
        combo_queued = 1;
      }
    }
    if (edge.potion && !player.attacking && !player.rolling && player.potions > 0.0f &&
        player.hp < player.maxhp && !player.dead) {
      player.potions -= 1.0f;
      player.hp += 42.0f;
      if (player.hp > player.maxhp) player.hp = player.maxhp;
      spark_burst(player.x, player.y, 60.0f, 120.0f, 6, 200, 240, 190);
    }

    player_update(dt, &edge);
    enemies_update(dt);
    sparks_update(dt);
    zone_update(dt);

    /* embers drifting off the three nearest fires, plus dust under the boots */
    ember_t += dt;
    if (ember_t > 0.075f && light_count > 0) {
      int i, k;
      float bd[3] = { 1e18f, 1e18f, 1e18f };
      int best[3] = { -1, -1, -1 };
      ember_t = 0.0f;
      for (i = 0; i < light_count; i++) {
        float d = dist2(lights[i].x, lights[i].y, player.x, player.y);
        if (d > 640.0f * 640.0f) continue;
        for (k = 0; k < 3; k++) {
          if (d < bd[k]) {
            int m;
            for (m = 2; m > k; m--) { bd[m] = bd[m - 1]; best[m] = best[m - 1]; }
            bd[k] = d; best[k] = i;
            break;
          }
        }
      }
      for (k = 0; k < 3; k++)
        if (best[k] >= 0)
          ember_emit(lights[best[k]].x, lights[best[k]].y, lights[best[k]].z + 6.0f);
    }

    if (player.rolling) dust_acc += dt;
    else if (player.moving && !player.dead) dust_acc += dt * (player.running ? 2.2f : 1.0f);
    if (dust_acc > 0.13f) {
      float bx = -sinf(player.yaw), by = cosf(player.yaw);
      dust_acc = 0.0f;
      dust_emit(player.x + bx * 12.0f, player.y + by * 12.0f, 3.0f);
      dust_emit(player.x - bx * 8.0f, player.y - by * 8.0f, 2.0f);
    }
  } else {
    if (!started) game_time += dt;
  }

  camera_update(dt, in);

  /* actor presence lights: a soft key hung between the camera and each
     fighter so nobody is reduced to a black shape against the night */
  dyn_light_clear();
  {
    int i;
    float hx = -cam_fwd_x * 62.0f, hy = -cam_fwd_y * 62.0f, hz = -cam_fwd_z * 62.0f;
    dyn_light_add(player.x + hx, player.y + hy, 92.0f + hz, 200.0f, 0.46f, 198, 206, 226);
    if (!started)
      dyn_light_add(player.x + hx * 0.55f, player.y + hy * 0.55f, 132.0f + hz * 0.5f,
                    150.0f, 0.95f, 250, 236, 210);
    for (i = 0; i < enemy_count; i++) {
      Enemy *e = &enemies[i];
      if (!e->alive) continue;
      if (dist2(e->x, e->y, player.x, player.y) > 620.0f * 620.0f) continue;
      dyn_light_add(e->x + hx, e->y + hy, 96.0f + hz, 215.0f, 0.78f, 236, 204, 162);
    }
    npcs_lights(player.x, player.y);
  }

  /* ---- draw ---- */
  plat_clear((int)(fog_r * 255.0f * 1.1f), (int)(fog_g * 255.0f * 1.1f),
             (int)(fog_b * 255.0f * 1.15f));
  draw_sky();

  mesh_reset();
  world_draw_ground();
  world_draw_props();
  actors_draw();
  npcs_draw();
  sparks_draw();
  draw_faces();

  cam.pitch = saved_pitch;
  cam.dist = saved_dist;

  draw_hud();
  plat_present();
}
