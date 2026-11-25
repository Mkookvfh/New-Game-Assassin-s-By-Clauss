/* THE ASHEN KNIGHT — mesh builder.
 *
 * Every visible polygon goes into one global array; draw_faces() projects the
 * lot, sorts it back to front and hands it to the platform.  The PS2 has no
 * usable Z-buffer in this setup, so painter's algorithm it is.
 */
#include <string.h>
#include <math.h>
#include "game.h"

Face faces[MAX_FACES];
int nfaces;

float fog_r = 0.06f, fog_g = 0.07f, fog_b = 0.09f;
float fog_near = 900.0f, fog_far = 2600.0f;

static int cur_r = 255, cur_g = 255, cur_b = 255;
static int cur_a = 128;
static int cur_add, cur_unlit, cur_tex;

void mesh_reset(void)
{
  nfaces = 0;
  cur_r = cur_g = cur_b = 255;
  cur_a = 128;
  cur_add = 0;
  cur_unlit = 0;
  cur_tex = 0;
}

void mesh_tint(int r, int g, int b) { cur_r = r; cur_g = g; cur_b = b; }
void mesh_alpha(int a) { cur_a = a < 0 ? 0 : (a > 128 ? 128 : a); }
void mesh_additive(int on) { cur_add = on ? 1 : 0; }
void mesh_unlit(int on) { cur_unlit = on ? 1 : 0; }
void face_texture(int tex) { cur_tex = tex; }

void cell_rect(int mat, float *u0, float *v0, float *u1, float *v1)
{
  float e = 0.0025f;
  if (mat < 0 || mat >= MAT_COUNT) {
    *u0 = 0.0f; *v0 = 0.0f; *u1 = 1.0f; *v1 = 1.0f;
    return;
  }
  float cx = (float)(mat & 3) * 0.25f;
  float cy = (float)(mat >> 2) * 0.25f;
  *u0 = cx + e;
  *v0 = cy + e;
  *u1 = cx + 0.25f - e;
  *v1 = cy + 0.25f - e;
}

/* ---------------------------------------------------------------- helpers */

static Face *face_new(int n)
{
  Face *f;
  if (nfaces >= MAX_FACES) return 0;
  f = &faces[nfaces++];
  f->n = (unsigned char)n;
  f->tex = (unsigned char)cur_tex;
  f->a = (unsigned char)cur_a;
  f->add = (unsigned char)cur_add;
  f->mat = (unsigned char)(cur_tex == 0 ? 0 : 0);
  f->r = (unsigned char)cur_r;
  f->g = (unsigned char)cur_g;
  f->b = (unsigned char)cur_b;
  return f;
}

/* Fake environment reflection.
 *
 * The GS cannot do real reflections, so metals and water get the trick every
 * PS2 game used: a Fresnel term (bright at grazing angles) plus a specular
 * highlight aimed at the moon.  Both are evaluated per face, so they cost a
 * handful of float ops instead of a second render pass.
 */
static void env_term(const Face *f, float nx, float ny, float nz,
                     float cx, float cy, float cz, float *r, float *g, float *b)
{
  /* moon sits at (-0.30,-0.90,0.34) * 5000 in game.c */
  const float mx = -0.30f, my = -0.90f, mz = 0.34f;
  int metal, water;
  float dx, dy, dz, inv, dot, ndv, fres, rx, ry, rz, spec;

  metal = (f->mat == MAT_IRON || f->mat == MAT_GOLD || f->mat == MAT_SLATE);
  water = (f->mat >= MAT_WATER0 && f->mat <= MAT_WATER3);
  if (!metal && !water) return;

  dx = cx - cam.x; dy = cy - cam.y; dz = cz - cam.z;
  inv = 1.0f / (sqrtf(dx * dx + dy * dy + dz * dz) + 0.001f);
  dx *= inv; dy *= inv; dz *= inv;

  dot = dx * nx + dy * ny + dz * nz;
  ndv = dot < 0.0f ? -dot : dot;
  fres = 1.0f - ndv;
  fres = fres * fres * fres;
  *r += fres * 0.075f; *g += fres * 0.095f; *b += fres * 0.130f;

  /* reflected view ray versus the moon direction, sharpened to a glint */
  rx = dx - 2.0f * dot * nx;
  ry = dy - 2.0f * dot * ny;
  rz = dz - 2.0f * dot * nz;
  spec = rx * mx + ry * my + rz * mz;
  if (spec > 0.0f) {
    spec = spec * spec; spec = spec * spec; spec = spec * spec;
    if (water) {
      float rip = 0.55f + 0.45f * sinf(cx * 0.07f + cy * 0.05f + game_time * 2.1f);
      spec *= 0.45f + 0.90f * rip;
    }
    *r += spec * 0.30f; *g += spec * 0.33f; *b += spec * 0.40f;
  }
}

static unsigned char shade_byte(float v)
{
  if (v <= 0.0f) return 0;
  if (v >= 1.0f) return 255;
  return (unsigned char)(v * 255.0f);
}

/* Two-sided lighting: evaluate both normals and keep the brighter result so
 * roughly-built walls never come out black.
 *
 * Large horizontal quads (ground tiles, pier decks) are lit per vertex
 * instead: that is the vertex lighting the PS2 is good at, and it turns the
 * hard square light pools into gradients that fade across the tile. */
static void shade_face(Face *f)
{
  float ax, ay, az, bx, by, bz, nx, ny, nz, len, cx, cy, cz;
  float r, g, b, r2, g2, b2, l1, l2;
  float tr = (float)cur_r / 255.0f;
  float tg = (float)cur_g / 255.0f;
  float tb = (float)cur_b / 255.0f;
  float ext = 0.0f;
  int i;

  f->gouraud = 0;
  if (cur_unlit) return;

  ax = f->x[1] - f->x[0]; ay = f->y[1] - f->y[0]; az = f->z[1] - f->z[0];
  bx = f->x[2] - f->x[0]; by = f->y[2] - f->y[0]; bz = f->z[2] - f->z[0];
  nx = ay * bz - az * by;
  ny = az * bx - ax * bz;
  nz = ax * by - ay * bx;
  len = sqrtf(nx * nx + ny * ny + nz * nz);
  if (len < 1e-6f) return;
  nx /= len; ny /= len; nz /= len;

  for (i = 1; i < f->n; i++) {
    float dx = fabsf(f->x[i] - f->x[0]);
    float dy = fabsf(f->y[i] - f->y[0]);
    float dz = fabsf(f->z[i] - f->z[0]);
    float m = maxf(maxf(dx, dy), dz);
    if (m > ext) ext = m;
  }

  /* ---- per-vertex path: big, upward-facing quads only ---------------- */
  if (f->n == 4 && nz > 0.86f && ext > 18.0f && ext < 100.0f) {
    for (i = 0; i < 4; i++) {
      float vr, vg, vb;
      light_rgb(f->x[i], f->y[i], f->z[i], nx, ny, nz, &vr, &vg, &vb);
      env_term(f, nx, ny, nz, f->x[i], f->y[i], f->z[i], &vr, &vg, &vb);
      f->vc[i][0] = shade_byte(vr * tr);
      f->vc[i][1] = shade_byte(vg * tg);
      f->vc[i][2] = shade_byte(vb * tb);
      if (i == 0) { r = vr; g = vg; b = vb; }
    }
    f->r = shade_byte(r * tr);
    f->g = shade_byte(g * tg);
    f->b = shade_byte(b * tb);
    f->gouraud = 1;
    return;
  }

  /* ---- flat path ---------------------------------------------------- */
  if (f->n == 4) {
    cx = (f->x[0] + f->x[1] + f->x[2] + f->x[3]) * 0.25f;
    cy = (f->y[0] + f->y[1] + f->y[2] + f->y[3]) * 0.25f;
    cz = (f->z[0] + f->z[1] + f->z[2] + f->z[3]) * 0.25f;
  } else {
    cx = (f->x[0] + f->x[1] + f->x[2]) / 3.0f;
    cy = (f->y[0] + f->y[1] + f->y[2]) / 3.0f;
    cz = (f->z[0] + f->z[1] + f->z[2]) / 3.0f;
  }

  light_rgb(cx, cy, cz, nx, ny, nz, &r, &g, &b);
  light_rgb(cx, cy, cz, -nx, -ny, -nz, &r2, &g2, &b2);
  l1 = r * 0.30f + g * 0.55f + b * 0.15f;
  l2 = r2 * 0.30f + g2 * 0.55f + b2 * 0.15f;
  if (l2 > l1) { r = r2; g = g2; b = b2; nx = -nx; ny = -ny; nz = -nz; }

  env_term(f, nx, ny, nz, cx, cy, cz, &r, &g, &b);

  f->r = shade_byte(r * tr);
  f->g = shade_byte(g * tg);
  f->b = shade_byte(b * tb);
}

/* ------------------------------------------------------------ primitives */

void qface(int mat, float x0, float y0, float z0, float x1, float y1, float z1,
           float x2, float y2, float z2, float x3, float y3, float z3,
           float u0, float v0, float u1, float v1)
{
  Face *f = face_new(4);
  if (!f) return;
  f->mat = (unsigned char)(mat < 0 ? 0 : mat);
  f->x[0] = x0; f->y[0] = y0; f->z[0] = z0;
  f->x[1] = x1; f->y[1] = y1; f->z[1] = z1;
  f->x[2] = x2; f->y[2] = y2; f->z[2] = z2;
  f->x[3] = x3; f->y[3] = y3; f->z[3] = z3;
  f->u[0] = u0; f->v[0] = v0;
  f->u[1] = u1; f->v[1] = v0;
  f->u[2] = u1; f->v[2] = v1;
  f->u[3] = u0; f->v[3] = v1;
  shade_face(f);
}

void qcell(int mat, float x0, float y0, float z0, float x1, float y1, float z1,
           float x2, float y2, float z2, float x3, float y3, float z3)
{
  float u0, v0, u1, v1;
  cell_rect(mat, &u0, &v0, &u1, &v1);
  qface(mat, x0, y0, z0, x1, y1, z1, x2, y2, z2, x3, y3, z3, u0, v0, u1, v1);
}

void tface(int mat, float x0, float y0, float z0, float x1, float y1, float z1,
           float x2, float y2, float z2, float u0, float v0, float u1, float v1,
           float u2, float v2)
{
  Face *f = face_new(3);
  if (!f) return;
  f->mat = (unsigned char)(mat < 0 ? 0 : mat);
  f->x[0] = x0; f->y[0] = y0; f->z[0] = z0;
  f->x[1] = x1; f->y[1] = y1; f->z[1] = z1;
  f->x[2] = x2; f->y[2] = y2; f->z[2] = z2;
  f->u[0] = u0; f->v[0] = v0;
  f->u[1] = u1; f->v[1] = v1;
  f->u[2] = u2; f->v[2] = v2;
  shade_face(f);
}

void shadow_blob(float x, float y, float rx, float ry, int a)
{
  int stex = cur_tex, sun = cur_unlit, sadd = cur_add, sa = cur_a;
  cur_tex = 1;
  cur_unlit = 1;
  cur_add = 0;
  cur_a = a < 0 ? 0 : (a > 128 ? 128 : a);
  qface(MAT_NONE, x - rx, y - ry, 1.0f, x + rx, y - ry, 1.0f,
        x + rx, y + ry, 1.0f, x - rx, y + ry, 1.0f, 0.02f, 0.02f, 0.98f, 0.98f);
  cur_tex = stex; cur_unlit = sun; cur_add = sadd; cur_a = sa;
}

void glow_billboard(float x, float y, float z, float size, int r, int g, int b, int a)
{
  float rx, ry, rz, ux, uy, uz;
  int stex = cur_tex, sun = cur_unlit, sadd = cur_add, sa = cur_a;
  int sr = cur_r, sg = cur_g, sb = cur_b;

  /* A flame keeps a roughly fixed angular size on screen: when the camera
   * walks right up to a brazier the glow must not swell into a white sheet. */
  {
    float dx = x - cam.x, dy = y - cam.y, dz = z - cam.z;
    float d = sqrtf(dx * dx + dy * dy + dz * dz);
    if (d < 300.0f) size *= 0.34f + 0.66f * (d / 300.0f);
  }

  /* a quad on the camera plane, so it faces the viewer from any angle */
  rx = cam_rgt_x * size; ry = cam_rgt_y * size; rz = cam_rgt_z * size;
  ux = cam_up_x * size; uy = cam_up_y * size; uz = cam_up_z * size;

  cur_tex = 2;
  cur_unlit = 1;
  cur_add = 1;
  cur_a = a;
  cur_r = r; cur_g = g; cur_b = b;
  qface(MAT_NONE,
        x - rx - ux, y - ry - uy, z - rz - uz,
        x + rx - ux, y + ry - uy, z + rz - uz,
        x + rx + ux, y + ry + uy, z + rz + uz,
        x - rx + ux, y - ry + uy, z - rz + uz,
        0.02f, 0.02f, 0.98f, 0.98f);
  cur_tex = stex; cur_unlit = sun; cur_add = sadd; cur_a = sa;
  cur_r = sr; cur_g = sg; cur_b = sb;
}

/* Warm pool of light lying flat on the floor: a radial additive decal that
 * keeps torch light attached to the ground instead of smearing up the screen. */
void light_pool(float x, float y, float r, int cr, int cg, int cb, int a)
{
  int stex = cur_tex, sun = cur_unlit, sadd = cur_add, sa = cur_a;
  int sr = cur_r, sg = cur_g, sb = cur_b;
  cur_tex = 2;
  cur_unlit = 1;
  cur_add = 1;
  cur_a = a;
  cur_r = cr; cur_g = cg; cur_b = cb;
  qface(MAT_NONE, x - r, y - r, 1.4f, x + r, y - r, 1.4f,
        x + r, y + r, 1.4f, x - r, y + r, 1.4f, 0.02f, 0.02f, 0.98f, 0.98f);
  cur_tex = stex; cur_unlit = sun; cur_add = sadd; cur_a = sa;
  cur_r = sr; cur_g = sg; cur_b = sb;
}

/* -------------------------------------------------------------- transforms */

Xf xf_id(void)
{
  Xf f;
  f.m[0] = 1; f.m[1] = 0; f.m[2] = 0;
  f.m[3] = 0; f.m[4] = 1; f.m[5] = 0;
  f.m[6] = 0; f.m[7] = 0; f.m[8] = 1;
  f.ox = f.oy = f.oz = 0;
  return f;
}

Xf xf_move(Xf p, float x, float y, float z)
{
  p.ox += p.m[0] * x + p.m[1] * y + p.m[2] * z;
  p.oy += p.m[3] * x + p.m[4] * y + p.m[5] * z;
  p.oz += p.m[6] * x + p.m[7] * y + p.m[8] * z;
  return p;
}

/* new matrix = R * p.m */
static Xf mul_pre(Xf p, const float *R)
{
  Xf f;
  int i, j;
  for (i = 0; i < 3; i++)
    for (j = 0; j < 3; j++)
      f.m[i * 3 + j] = R[i * 3 + 0] * p.m[0 * 3 + j] +
                       R[i * 3 + 1] * p.m[1 * 3 + j] +
                       R[i * 3 + 2] * p.m[2 * 3 + j];
  f.ox = p.ox; f.oy = p.oy; f.oz = p.oz;
  return f;
}

Xf xf_rotx(Xf p, float a)
{
  float c = cosf(a), s = sinf(a);
  float R[9] = { 1, 0, 0,  0, c, -s,  0, s, c };
  return mul_pre(p, R);
}

Xf xf_roty(Xf p, float a)
{
  float c = cosf(a), s = sinf(a);
  float R[9] = { c, 0, s,  0, 1, 0,  -s, 0, c };
  return mul_pre(p, R);
}

Xf xf_rotz(Xf p, float a)
{
  float c = cosf(a), s = sinf(a);
  float R[9] = { c, -s, 0,  s, c, 0,  0, 0, 1 };
  return mul_pre(p, R);
}

Xf xf_pivot_rotx(Xf p, float a, float py, float pz)
{
  Xf t = xf_move(p, 0.0f, -py, -pz);
  t = xf_rotx(t, a);
  return xf_move(t, 0.0f, py, pz);
}

V3 xf_pt(Xf f, float x, float y, float z)
{
  V3 v;
  v.x = f.m[0] * x + f.m[1] * y + f.m[2] * z + f.ox;
  v.y = f.m[3] * x + f.m[4] * y + f.m[5] * z + f.oy;
  v.z = f.m[6] * x + f.m[7] * y + f.m[8] * z + f.oz;
  return v;
}

void mq(Xf f, int mat, float ax, float ay, float az, float bx, float by, float bz,
        float cx, float cy, float cz, float dx, float dy, float dz)
{
  V3 A = xf_pt(f, ax, ay, az), B = xf_pt(f, bx, by, bz);
  V3 C = xf_pt(f, cx, cy, cz), D = xf_pt(f, dx, dy, dz);
  qcell(mat, A.x, A.y, A.z, B.x, B.y, B.z, C.x, C.y, C.z, D.x, D.y, D.z);
}

void xf_box(Xf f, int mat, float cx, float cy, float cz,
            float hx, float hy, float hz, int caps)
{
  float u0, v0, u1, v1;
  V3 a, b, c, d;
  cell_rect(mat, &u0, &v0, &u1, &v1);

#define Q(A0,A1,A2,B0,B1,B2,C0,C1,C2,D0,D1,D2) do { \
    a = xf_pt(f, (A0), (A1), (A2)); b = xf_pt(f, (B0), (B1), (B2)); \
    c = xf_pt(f, (C0), (C1), (C2)); d = xf_pt(f, (D0), (D1), (D2)); \
    qface(mat, a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z, d.x, d.y, d.z, \
          u0, v0, u1, v1); \
  } while (0)

  /* +x / -x */
  Q(cx + hx, cy - hy, cz - hz, cx + hx, cy + hy, cz - hz,
    cx + hx, cy + hy, cz + hz, cx + hx, cy - hy, cz + hz);
  Q(cx - hx, cy + hy, cz - hz, cx - hx, cy - hy, cz - hz,
    cx - hx, cy - hy, cz + hz, cx - hx, cy + hy, cz + hz);
  /* +y / -y */
  Q(cx + hx, cy + hy, cz - hz, cx - hx, cy + hy, cz - hz,
    cx - hx, cy + hy, cz + hz, cx + hx, cy + hy, cz + hz);
  Q(cx - hx, cy - hy, cz - hz, cx + hx, cy - hy, cz - hz,
    cx + hx, cy - hy, cz + hz, cx - hx, cy - hy, cz + hz);
  /* top */
  if (caps) {
    Q(cx - hx, cy - hy, cz + hz, cx + hx, cy - hy, cz + hz,
      cx + hx, cy + hy, cz + hz, cx - hx, cy + hy, cz + hz);
    Q(cx - hx, cy + hy, cz - hz, cx + hx, cy + hy, cz - hz,
      cx + hx, cy - hy, cz - hz, cx - hx, cy - hy, cz - hz);
  }
#undef Q
}

/* ------------------------------------------------------------------- draw */

typedef struct { float dep, sx[4], sy[4], u[4], v[4]; } ProjFace;
static ProjFace pbuf[MAX_FACES];
static unsigned short fidx[MAX_FACES];
static unsigned short order[MAX_FACES];
static unsigned short bucket[MAX_FACES];
#define NSORT 1024

/* camera basis, filled by game.c each frame */
float cam_fwd_x = 0.0f, cam_fwd_y = 1.0f, cam_fwd_z = 0.0f;
float cam_rgt_x = 1.0f, cam_rgt_y = 0.0f, cam_rgt_z = 0.0f;
float cam_up_x = 0.0f, cam_up_y = 0.0f, cam_up_z = 1.0f;

void draw_faces(void)
{
  int i, j, np = 0;
  float fog_span = fog_far - fog_near;
  if (fog_span < 1.0f) fog_span = 1.0f;

  for (i = 0; i < nfaces; i++) {
    Face *f = &faces[i];
    ProjFace *p = &pbuf[np];
    float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    int ok = 1;
    float dep_sum = 0.0f;

    for (j = 0; j < f->n; j++) {
      float dx = f->x[j] - cam.x, dy = f->y[j] - cam.y, dz = f->z[j] - cam.z;
      float vz = dx * cam_fwd_x + dy * cam_fwd_y + dz * cam_fwd_z;
      if (vz < 14.0f) { ok = 0; break; }          /* near reject */
      dep_sum += vz;
      if (!proj(f->x[j], f->y[j], f->z[j], &p->sx[j], &p->sy[j], &vz)) { ok = 0; break; }
      if (p->sx[j] < minx) minx = p->sx[j];
      if (p->sx[j] > maxx) maxx = p->sx[j];
      if (p->sy[j] < miny) miny = p->sy[j];
      if (p->sy[j] > maxy) maxy = p->sy[j];
      p->u[j] = f->u[j];
      p->v[j] = f->v[j];
    }
    if (!ok) continue;
    if (maxx < -8.0f || minx > SCR_W + 8.0f) continue;
    if (maxy < -8.0f || miny > SCR_H + 8.0f) continue;
    p->dep = dep_sum / (float)f->n;
    fidx[np] = (unsigned short)i;
    np++;
  }

  /* counting sort, far to near; bucket width ~2-3 world units */
  {
    float dmin = 1e9f, dmax = -1e9f, scale;
    static unsigned short cnt[NSORT + 1];
    for (i = 0; i < np; i++) {
      if (pbuf[i].dep < dmin) dmin = pbuf[i].dep;
      if (pbuf[i].dep > dmax) dmax = pbuf[i].dep;
    }
    if (dmax - dmin < 1.0f) dmax = dmin + 1.0f;
    scale = (float)NSORT / (dmax - dmin);
    for (i = 0; i <= NSORT; i++) cnt[i] = 0;
    for (i = 0; i < np; i++) {
      int b = (int)((dmax - pbuf[i].dep) * scale);
      if (b < 0) b = 0;
      if (b > NSORT - 1) b = NSORT - 1;
      bucket[i] = (unsigned short)b;
      cnt[b + 1]++;
    }
    for (i = 0; i < NSORT; i++) cnt[i + 1] += cnt[i];
    for (i = 0; i < np; i++) order[cnt[bucket[i]]++] = (unsigned short)i;
  }

  for (i = 0; i < np; i++) {
    ProjFace *p = &pbuf[order[i]];
    Face *f = &faces[fidx[order[i]]];
    int r = f->r, g = f->g, b = f->b;
    const unsigned char *cv = 0;
    unsigned char fogged[12];

    if (!f->add) {
      float t = (p->dep - fog_near) / fog_span;
      if (t > 0.0f) {
        float fr = fog_r * 255.0f, fg = fog_g * 255.0f, fb = fog_b * 255.0f;
        int k;
        if (t > 1.0f) t = 1.0f;
        r = (int)((float)r + (fr - (float)r) * t);
        g = (int)((float)g + (fg - (float)g) * t);
        b = (int)((float)b + (fb - (float)b) * t);
        if (f->gouraud) {
          for (k = 0; k < 4; k++) {
            fogged[k * 3 + 0] = (unsigned char)((float)f->vc[k][0] +
                                               (fr - (float)f->vc[k][0]) * t);
            fogged[k * 3 + 1] = (unsigned char)((float)f->vc[k][1] +
                                               (fg - (float)f->vc[k][1]) * t);
            fogged[k * 3 + 2] = (unsigned char)((float)f->vc[k][2] +
                                               (fb - (float)f->vc[k][2]) * t);
          }
          cv = fogged;
        }
      } else if (f->gouraud) {
        cv = (const unsigned char *)f->vc;
      }
    }
    plat_tri(p->sx, p->sy, p->u, p->v, f->n, f->tex, f->mat,
             r, g, b, f->a, f->add, cv);
  }
}

