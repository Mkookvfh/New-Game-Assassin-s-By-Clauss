/* THE ASHEN KNIGHT — host preview backend.
 *
 * Plain old C software rasteriser that implements the plat_* API so the exact
 * same game code can be rendered on a PC for a look-dev check.  No z-buffer:
 * the face list is already sorted back to front, so this just paints in order
 * exactly like the PS2 build does.
 *
 * build: make -f Makefile.preview
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "game.h"

static unsigned char fb[SCR_H][SCR_W][3];
static unsigned char shadow_tex[128 * 128 * 4];
static unsigned char glow_tex[128 * 128 * 4];
static unsigned char font_tex[FONT_TEX_W * FONT_TEX_H * 4];
static int g_written;

/* ------------------------------------------------------------- textures */

static void build_aux_tex(void)
{
  int x, y;
  for (y = 0; y < 128; y++) {
    for (x = 0; x < 128; x++) {
      float dx = (x + 0.5f) / 64.0f - 1.0f;
      float dy = (y + 0.5f) / 64.0f - 1.0f;
      float d = sqrtf(dx * dx + dy * dy);
      int i = (y * 128 + x) * 4;
      float a = 1.0f - d;
      if (a < 0.0f) a = 0.0f;
      a = a * a * (3.0f - 2.0f * a);
      shadow_tex[i + 0] = 0;
      shadow_tex[i + 1] = 0;
      shadow_tex[i + 2] = 0;
      shadow_tex[i + 3] = (unsigned char)(a * 220.0f);
      glow_tex[i + 0] = 255;
      glow_tex[i + 1] = 255;
      glow_tex[i + 2] = 255;
      glow_tex[i + 3] = (unsigned char)(a * 255.0f);
    }
  }
}

static void sample(int tex, float u, float v, int *r, int *g, int *b, int *a)
{
  if (u < 0.0f) u = 0.0f;
  if (u > 0.9999f) u = 0.9999f;
  if (v < 0.0f) v = 0.0f;
  if (v > 0.9999f) v = 0.9999f;
  if (tex == 0) {
    int x = (int)(u * 256.0f), y = (int)(v * 256.0f);
    const unsigned char *p = materials_atlas + (y * 256 + x) * 4;
    *r = p[0]; *g = p[1]; *b = p[2]; *a = p[3];
  } else {
    const unsigned char *t = (tex == 1) ? shadow_tex : glow_tex;
    int x = (int)(u * 128.0f), y = (int)(v * 128.0f);
    const unsigned char *p = t + (y * 128 + x) * 4;
    *r = p[0]; *g = p[1]; *b = p[2]; *a = p[3];
  }
}

/* -------------------------------------------------------------- raster */

static inline int cl(double v) { return v < 0 ? 0 : (v > 255 ? 255 : (int)v); }

static inline void blend(int x, int y, float r, float g, float b, float a, int add)
{
  unsigned char *p = fb[y][x];
  if (a <= 0.0f) return;
  if (add) {
    p[0] = (unsigned char)cl(p[0] + r * a);
    p[1] = (unsigned char)cl(p[1] + g * a);
    p[2] = (unsigned char)cl(p[2] + b * a);
  } else {
    p[0] = (unsigned char)cl(p[0] * (1.0f - a) + r * a);
    p[1] = (unsigned char)cl(p[1] * (1.0f - a) + g * a);
    p[2] = (unsigned char)cl(p[2] * (1.0f - a) + b * a);
  }
}

static void raster(int tex, int cr, int cg, int cb, const float *vc,
                   float ca, int add,
                   float ax, float ay, float au, float av,
                   float bx, float by, float bu, float bv,
                   float cx, float cy, float cu, float cv)
{
  int minx, maxx, miny, maxy, x, y;
  float area, inv;

  minx = (int)floorf(fminf(ax, fminf(bx, cx)));
  maxx = (int)ceilf(fmaxf(ax, fmaxf(bx, cx)));
  miny = (int)floorf(fminf(ay, fminf(by, cy)));
  maxy = (int)ceilf(fmaxf(ay, fmaxf(by, cy)));
  if (minx < 0) minx = 0;
  if (miny < 0) miny = 0;
  if (maxx > SCR_W - 1) maxx = SCR_W - 1;
  if (maxy > SCR_H - 1) maxy = SCR_H - 1;
  if (minx > maxx || miny > maxy) return;

  area = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
  if (fabsf(area) < 1e-5f) return;
  inv = 1.0f / area;

  for (y = miny; y <= maxy; y++) {
    float py = (float)y + 0.5f;
    for (x = minx; x <= maxx; x++) {
      float px = (float)x + 0.5f;
      float w0 = ((bx - px) * (cy - py) - (cx - px) * (by - py)) * inv;
      float w1 = ((cx - px) * (ay - py) - (ax - px) * (cy - py)) * inv;
      float w2 = 1.0f - w0 - w1;
      float uu, vv, alpha, fr = (float)cr, fg = (float)cg, fb = (float)cb;
      int tr, tg, tb, ta;
      if (w0 < -0.0002f || w1 < -0.0002f || w2 < -0.0002f) continue;
      uu = au * w0 + bu * w1 + cu * w2;
      vv = av * w0 + bv * w1 + cv * w2;
      sample(tex, uu, vv, &tr, &tg, &tb, &ta);
      alpha = (float)ta * (1.0f / 255.0f) * ca;
      if (alpha <= 0.001f) continue;
      if (vc) {                       /* Gouraud: shade per pixel */
        int k0 = 0, k1 = 3, k2 = 6;
        fr = vc[k0 + 0] * w0 + vc[k1 + 0] * w1 + vc[k2 + 0] * w2;
        fg = vc[k0 + 1] * w0 + vc[k1 + 1] * w1 + vc[k2 + 1] * w2;
        fb = vc[k0 + 2] * w0 + vc[k1 + 2] * w1 + vc[k2 + 2] * w2;
      }
      blend(x, y, (float)tr * fr * (1.0f / 255.0f),
            (float)tg * fg * (1.0f / 255.0f),
            (float)tb * fb * (1.0f / 255.0f), alpha, add);
    }
  }
}

/* ------------------------------------------------------------ plat API */

void plat_clear(int r, int g, int b)
{
  int i;
  unsigned char *p = &fb[0][0][0];
  for (i = 0; i < SCR_W * SCR_H; i++) {
    p[0] = (unsigned char)r; p[1] = (unsigned char)g; p[2] = (unsigned char)b;
    p += 3;
  }
  g_written = 1;
}

void plat_tri(const float *sx, const float *sy, const float *u, const float *v,
              int n, int tex, int mat, int r, int g, int b, int a, int add,
              const unsigned char *cv)
{
  float ca = (float)a / 128.0f;
  float vc[9];
  int k;
  (void)mat;
  if (ca > 1.0f) ca = 1.0f;
  if (n < 3) return;
  if (cv) for (k = 0; k < 9; k++) vc[k] = (float)cv[k];
  raster(tex, r, g, b, cv ? vc : 0, ca, add, sx[0], sy[0], u[0], v[0],
         sx[1], sy[1], u[1], v[1], sx[2], sy[2], u[2], v[2]);
  if (n == 4)
    raster(tex, r, g, b, cv ? vc : 0, ca, add, sx[0], sy[0], u[0], v[0],
           sx[2], sy[2], u[2], v[2], sx[3], sy[3], u[3], v[3]);
}

void plat_rect(float x, float y, float w, float h, int r, int g, int b, int a)
{
  int x0 = (int)floorf(x), y0 = (int)floorf(y);
  int x1 = (int)ceilf(x + w), y1 = (int)ceilf(y + h);
  float ca = (float)a / 128.0f;
  int px, py;
  if (ca > 1.0f) ca = 1.0f;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > SCR_W) x1 = SCR_W;
  if (y1 > SCR_H) y1 = SCR_H;
  for (py = y0; py < y1; py++)
    for (px = x0; px < x1; px++)
      blend(px, py, (float)r, (float)g, (float)b, ca, 0);
}

void plat_text(float x, float y, float scale, const char *s, int r, int g, int b, int a)
{
  float ca = (float)a / 128.0f;
  int cx = (int)floorf(x);
  if (ca > 1.0f) ca = 1.0f;
  while (s && *s) {
    unsigned char c = (unsigned char)*s++;
    int gi, gy, gx;
    if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
    if (c < 32 || c > 126) { cx += (int)(6.0f * scale); continue; }
    gi = c - 32;
    {
      int bx = (gi % FONT_COLS) * FONT_CELL_W;
      int by = (gi / FONT_COLS) * FONT_CELL_H;
      for (gy = 0; gy < 8; gy++) {
        for (gx = 0; gx < 6; gx++) {
          const unsigned char *p = font_tex + (((by + gy) * FONT_TEX_W) + bx + gx) * 4;
          if (p[3] == 0) continue;
          plat_rect((float)cx + gx * scale, y + gy * scale, scale, scale, r, g, b, a);
        }
      }
    }
    cx += (int)(6.0f * scale);
  }
  (void)ca;
}

void plat_present(void) { g_written = 1; }

/* --------------------------------------------------------------- output */

static int write_ppm(const char *path)
{
  FILE *f = fopen(path, "wb");
  int y;
  if (!f) { fprintf(stderr, "cannot write %s\n", path); return 1; }
  fprintf(f, "P6\n%d %d\n255\n", SCR_W, SCR_H);
  for (y = 0; y < SCR_H; y++) fwrite(fb[y], 1, SCR_W * 3, f);
  fclose(f);
  return 0;
}

/* ---------------------------------------------------------------- shots */

static void step(float dt, Input *in, int n)
{
  int i;
  for (i = 0; i < n; i++) game_frame(dt, in, 0);
}

static void park_enemies(int keep)
{
  int i;
  for (i = 0; i < enemy_count; i++) {
    if (i == keep) continue;
    enemies[i].x = -4000.0f - i * 60.0f;
    enemies[i].y = -4000.0f;
    enemies[i].state = 0;
    enemies[i].cd = 999.0f;
  }
}

int main(int argc, char **argv)
{
  const char *out = (argc > 1) ? argv[1] : "shot.ppm";
  const char *shot = (argc > 2) ? argv[2] : "courtyard";
  Input in;
  int i;

  build_aux_tex();
  font_build(font_tex, FONT_TEX_W, FONT_TEX_H);

  if (!strcmp(shot, "texcheck")) {
    float sx[4], sy[4], u[4], v[4];
    int m;
    plat_clear(20, 20, 24);
    for (m = 0; m < 16; m++) {
      int cxi = (m & 3) * 160, cyi = (m >> 2) * 112;
      u[0] = (float)(m & 3) * 0.25f + 0.002f; v[0] = (float)(m >> 2) * 0.25f + 0.002f;
      u[1] = u[0] + 0.246f; v[1] = v[0];
      u[2] = u[0] + 0.246f; v[2] = v[0] + 0.246f;
      u[3] = u[0];         v[3] = v[0] + 0.246f;
      sx[0] = (float)cxi;      sy[0] = (float)cyi;
      sx[1] = (float)cxi + 156; sy[1] = (float)cyi;
      sx[2] = (float)cxi + 156; sy[2] = (float)cyi + 108;
      sx[3] = (float)cxi;      sy[3] = (float)cyi + 108;
      plat_tri(sx, sy, u, v, 4, 0, m, 255, 255, 255, 128, 0, 0);
    }
    return write_ppm(out);
  }

  memset(&in, 0, sizeof(in));
  game_init();
  for (i = 0; i < MAX_ENEMIES; i++) if (i < enemy_count) enemies[i].cd = 999.0f;

  if (!strcmp(shot, "title")) {
    step(1.0f / 60.0f, &in, 10);
    write_ppm(out);
    return 0;
  }

  in.start = 1;
  step(1.0f / 60.0f, &in, 1);
  in.start = 0;

  if (!strcmp(shot, "courtyard")) {
    park_enemies(-1);
    player.x = 896.0f; player.y = 960.0f; player.yaw = 0.0f;
    cam.yaw = 0.0f; cam.pitch = 0.22f;
    step(1.0f / 60.0f, &in, 24);
  } else if (!strcmp(shot, "bridge")) {
    park_enemies(-1);
    player.x = 860.0f; player.y = 1700.0f; player.yaw = 3.14159f;
    cam.yaw = 3.14159f; cam.pitch = 0.30f;
    step(1.0f / 60.0f, &in, 24);
  } else if (!strcmp(shot, "town")) {
    park_enemies(-1);
    player.x = 948.0f; player.y = 2530.0f; player.yaw = 3.14159f;
    cam.yaw = 3.14159f; cam.pitch = 0.20f;
    step(1.0f / 60.0f, &in, 24);
  } else if (!strcmp(shot, "harbour")) {
    park_enemies(-1);
    player.x = 860.0f; player.y = 3430.0f; player.yaw = 2.65f;
    cam.yaw = 2.65f; cam.pitch = 0.17f;
    step(1.0f / 60.0f, &in, 24);
  } else if (!strcmp(shot, "gate")) {
    park_enemies(-1);
    player.x = 896.0f; player.y = 1240.0f; player.yaw = 3.14159f;
    cam.yaw = 3.14159f; cam.pitch = 0.20f;
    step(1.0f / 60.0f, &in, 24);
  } else if (!strcmp(shot, "portrait")) {
    park_enemies(-1);
    player.x = 896.0f; player.y = 980.0f; player.yaw = 0.0f;
    cam.yaw = 3.14159f; cam.pitch = 0.08f; cam.dist = 118.0f;
    step(1.0f / 60.0f, &in, 24);
  } else if (!strcmp(shot, "folk")) {
    park_enemies(-1);
    player.x = 760.0f; player.y = 3430.0f; player.yaw = 0.0f;
    cam.yaw = -0.38f; cam.pitch = 0.10f; cam.dist = 140.0f;
    step(1.0f / 60.0f, &in, 24);
  } else if (!strcmp(shot, "horde")) {
    int k;
    player.x = 896.0f; player.y = 1000.0f; player.yaw = 3.14159f;
    cam.yaw = 3.14159f; cam.pitch = 0.20f;
    for (k = 0; k < enemy_count; k++) {
      if (k < 4) {
        float a = (float)k * 1.25f;
        enemies[k].x = 896.0f + cosf(a) * 180.0f;
        enemies[k].y = 1000.0f + sinf(a) * 180.0f;
      } else {
        enemies[k].x = -4000.0f - k * 60.0f;
        enemies[k].y = -4000.0f;
      }
      enemies[k].state = 0; enemies[k].cd = 999.0f; enemies[k].alive = 1;
    }
    step(1.0f / 60.0f, &in, 20);
  } else if (!strcmp(shot, "fight")) {
    park_enemies(0);
    player.x = 862.0f; player.y = 800.0f; player.yaw = 0.0f;
    cam.yaw = 0.0f; cam.pitch = 0.13f; cam.dist = 158.0f;
    enemies[0].x = 898.0f; enemies[0].y = 726.0f;
    enemies[0].yaw = 3.14159f;
    enemies[0].state = 2; enemies[0].windup = 0.34f; enemies[0].cd = 999.0f;
    in.lock = 1;
    step(1.0f / 60.0f, &in, 20);
    enemies[0].state = 2; enemies[0].windup = 0.34f;
    player.attacking = 1;
    player.atk_t = 0.41f;
    player.atk_stage = 0.0f;
  } else {
    step(1.0f / 60.0f, &in, 10);
  }

  /* final frame is rendered with dt 0 so the pose we set survives */
  game_frame(0.0f, &in, 0);
  fprintf(stderr, "nfaces=%d props=%d lights=%d player=(%.0f,%.0f) cam=(%.0f,%.0f,%.0f)\n",
          nfaces, prop_count, light_count, player.x, player.y, cam.x, cam.y, cam.z);
  (void)g_written;
  return write_ppm(out);
}
