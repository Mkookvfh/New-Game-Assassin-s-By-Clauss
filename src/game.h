/* THE ASHEN KNIGHT — PS2 homebrew, third-person action prototype.
 * game.h : shared types, the platform interface and the game entry point.
 *
 * Everything in this file is free of gsKit so the same scene code runs on the
 * PS2 and in the host preview renderer (tools/preview_host.c).
 */
#ifndef GAME_H
#define GAME_H

#include <math.h>

#define SCR_W 640
#define SCR_H 448

#define TILE 64
#define MAP_W 64
#define MAP_H 64
#define WORLD_W (MAP_W * TILE)
#define WORLD_H (MAP_H * TILE)

/* ---- material atlas (tools/gen_materials.py) -------------------------- */
enum {
  MAT_STONE, MAT_COBBLE, MAT_FLAG, MAT_PLANK,
  MAT_WATER0, MAT_WATER1, MAT_WATER2, MAT_WATER3,
  MAT_SLATE, MAT_LIME, MAT_IRON, MAT_BANNER,
  MAT_EARTH, MAT_MOSS, MAT_GOLD, MAT_DARK,
  MAT_COUNT
};
extern const unsigned char materials_atlas[];
#define MAT_ATLAS_W 256
#define MAT_ATLAS_H 256

#define MAT_NONE (-1)          /* untextured, colour only */

/* distance fog, tinted per zone by world.c / game.c */
extern float fog_r, fog_g, fog_b, fog_near, fog_far;
extern float game_time;

/* ---- ground tiles ----------------------------------------------------- */
enum { T_COBBLE, T_FLAG, T_PLANK, T_EARTH, T_MOSS, T_WATER, T_VOID };

/* ---- platform --------------------------------------------------------
 * tex: 0 = material atlas, 1 = soft shadow, 2 = radial glow.
 */
void plat_clear(int r, int g, int b);
/* cv, when non-NULL, points at n*3 bytes of per-vertex colour (r0,g0,b0,
 * r1,g1,b1, ...) for Gouraud (vertex-lit) shading; r/g/b are then ignored. */
void plat_tri(const float *sx, const float *sy, const float *u, const float *v,
              int n, int tex, int mat, int r, int g, int b, int a, int add,
              const unsigned char *cv);
void plat_rect(float x, float y, float w, float h, int r, int g, int b, int a);
void plat_text(float x, float y, float scale, const char *s, int r, int g, int b, int a);
void plat_present(void);

/* ---- small helpers ---------------------------------------------------- */
static inline float clampf(float v, float lo, float hi)
{
  return v < lo ? lo : (v > hi ? hi : v);
}
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float maxf(float a, float b) { return a > b ? a : b; }
static inline float minf(float a, float b) { return a < b ? a : b; }
static inline float wrap_pi(float a)
{
  while (a > 3.14159265f) a -= 6.28318531f;
  while (a < -3.14159265f) a += 6.28318531f;
  return a;
}

/* ---- world ------------------------------------------------------------ */
extern unsigned char tiles[MAP_H][MAP_W];
extern float water_time;

int tile_solid_at(float x, float y);
void world_gen(void);
void world_draw_ground(void);
void world_draw_props(void);

#define MAX_PROPS 520
enum {
  P_WALL, P_TOWER, P_GATE, P_BRAZIER, P_BARREL, P_CRATE,
  P_BOAT, P_HOUSE, P_BANNER, P_POST, P_STATUE, P_PIER, P_CART
};
typedef struct {
  short x, y, z;            /* base centre */
  short hw, hd, h;          /* half extents / height (collision + build) */
  unsigned char type, sub;
} Prop;
extern Prop props[MAX_PROPS];
extern int prop_count;
int prop_blocks(float x, float y, float r);

#define MAX_LIGHTS 28
typedef struct {
  float x, y, z, r;          /* position + radius */
  float cr, cg, cb, k;       /* colour and intensity */
  float phase;
} Light;
extern Light lights[MAX_LIGHTS];
extern int light_count;
void light_rgb(float x, float y, float z, float nx, float ny, float nz,
               float *r, float *g, float *b);
void light_add(float x, float y, float z, float r, float cr, float cg, float cb, float k);

/* per-frame lights that ride with the actors so they never read as
   silhouettes: cleared and refilled at the start of every frame */
void dyn_light_clear(void);
void dyn_light_add(float x, float y, float z, float r, float k, int cr, int cg, int cb);

/* ---- mesh builder (mesh.c) -------------------------------------------- */
typedef struct { float x, y, z; } V3;

typedef struct {
  float x[4], y[4], z[4];
  float u[4], v[4];
  unsigned char n, tex, mat, add, a, r, g, b;
  unsigned char gouraud;        /* use vc[] instead of the flat rgb */
  unsigned char vc[4][3];       /* per-vertex rgb for Gouraud shading */
} Face;

#define MAX_FACES 3400
extern Face faces[MAX_FACES];
extern int nfaces;

void mesh_reset(void);
void mesh_tint(int r, int g, int b);
void mesh_alpha(int a);
void mesh_additive(int on);
void mesh_unlit(int on);
void qface(int mat, float x0, float y0, float z0, float x1, float y1, float z1,
           float x2, float y2, float z2, float x3, float y3, float z3,
           float u0, float v0, float u1, float v1);
void qcell(int mat, float x0, float y0, float z0, float x1, float y1, float z1,
           float x2, float y2, float z2, float x3, float y3, float z3);
void tface(int mat, float x0, float y0, float z0, float x1, float y1, float z1,
           float x2, float y2, float z2, float u0, float v0, float u1, float v1,
           float u2, float v2);
void cell_rect(int mat, float *u0, float *v0, float *u1, float *v1);
void face_texture(int tex);          /* 0 atlas / 1 shadow / 2 glow */
void shadow_blob(float x, float y, float rx, float ry, int a);
void glow_billboard(float x, float y, float z, float size, int r, int g, int b, int a);
void light_pool(float x, float y, float r, int cr, int cg, int cb, int a);
void draw_faces(void);               /* project, sort far->near, emit */
extern float cam_fwd_x, cam_fwd_y, cam_fwd_z;
extern float cam_rgt_x, cam_rgt_y, cam_rgt_z;
extern float cam_up_x, cam_up_y, cam_up_z;

/* transforms */
typedef struct { float m[9]; float ox, oy, oz; } Xf;
Xf xf_id(void);
Xf xf_move(Xf p, float x, float y, float z);
Xf xf_rotx(Xf p, float a);
Xf xf_roty(Xf p, float a);
Xf xf_rotz(Xf p, float a);
Xf xf_pivot_rotx(Xf p, float a, float py, float pz);
V3 xf_pt(Xf f, float x, float y, float z);
void xf_box(Xf f, int mat, float cx, float cy, float cz,
            float hx, float hy, float hz, int caps);
/* quad in local space through a transform */
void mq(Xf f, int mat, float ax, float ay, float az, float bx, float by, float bz,
        float cx, float cy, float cz, float dx, float dy, float dz);

/* ---- camera / actors (game.c) ----------------------------------------- */
typedef struct {
  float x, y, z, yaw, pitch, dist, focal;
} Cam;
extern Cam cam;

int proj(float wx, float wy, float wz, float *sx, float *sy, float *dep);
void project_3d(float wx, float wy, float wz, float *sx, float *sy);

typedef struct {
  float x, y, z, yaw;
  float hp, maxhp, sta, potions;
  float anim;              /* gait phase */
  float atk_t, atk_stage;  /* combo clock */
  float roll_t, hurt_t, dead_t;
  float speed;
  int moving, running, rolling, attacking, dead, lock;
} Player;
extern Player player;

typedef struct {
  float x, y, yaw;
  float hp, maxhp;
  float anim, cd, windup, strike, recover, hurt, dead_t;
  float vx, vy;
  int state, type, flash, alive;
} Enemy;
#define MAX_ENEMIES 12
extern Enemy enemies[MAX_ENEMIES];
extern int enemy_count;

typedef struct {
  float mx, my;            /* left stick: strafe, forward */
  float rx, ry;            /* right stick: orbit camera, pitch */
  int attack, roll, lock, potion, run, start, back;
} Input;

void actors_draw(void);
void npcs_draw(void);
void npcs_lights(float cx, float cy);

void game_init(void);
void game_frame(float dt, const Input *in, int paused);
int game_started(void);

/* text helpers (font.c).  The glyph strip is packed into a power-of-two
 * texture because the GS only accepts POT dimensions. */
#define FONT_COLS 16
#define FONT_ROWS 6
#define FONT_CELL_W 6
#define FONT_CELL_H 8
#define FONT_TEX_W 128
#define FONT_TEX_H 64
void font_build(unsigned char *dst, int w, int h);
float text_width(const char *s, float scale);

#endif
