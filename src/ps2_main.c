/* THE ASHEN KNIGHT — PlayStation 2 backend.
 *
 * gsKit drawing backend for the portable scene code.  The game itself
 * (src/game.c, world.c, actors.c, mesh.c) knows nothing about the PS2; it
 * fills a face list and calls the plat_* functions below.
 *
 * Rendering: the GS has no usable z-buffer for this kind of scene, so the
 * faces arrive already sorted far->near and are emitted in that order with
 * z-testing off (the same painter's algorithm a 2005-era PS2 game would use).
 */

#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <gsKit.h>
#include <dmaKit.h>
#include <gsToolkit.h>
#include <libpad.h>

#include "game.h"

extern unsigned char irx_sio2man[];
extern unsigned int size_irx_sio2man;
extern unsigned char irx_padman[];
extern unsigned int size_irx_padman;

/* ------------------------------------------------------------------ video */

static GSGLOBAL *gs;
static GSTEXTURE tex_mat, tex_shadow, tex_glow, tex_font, tex_white;

static unsigned char aux_shadow[128 * 128 * 4] __attribute__((aligned(128)));
static unsigned char aux_glow[128 * 128 * 4] __attribute__((aligned(128)));
static unsigned char font_buf[FONT_TEX_W * FONT_TEX_H * 4] __attribute__((aligned(128)));
static unsigned char white_buf[8 * 8 * 4] __attribute__((aligned(128)));

#define COL(r, g, b, a) ((u64)GS_SETREG_RGBAQ((r), (g), (b), (a), 0))

extern void font_build(unsigned char *dst, int w, int h);

static void make_texture(GSTEXTURE *t, const unsigned char *data, int w, int h)
{
  t->Width = w;
  t->Height = h;
  t->PSM = GS_PSM_CT32;
  t->Filter = GS_FILTER_NEAREST;
  t->Mem = (u32 *)data;
  t->Delayed = 0;
  t->Clut = NULL;
  t->VramClut = 0;
  t->Vram = gsKit_vram_alloc(gs, gsKit_texture_size(w, h, GS_PSM_CT32),
                             GSKIT_ALLOC_USERBUFFER);
  gsKit_texture_upload(gs, t);
}

/* radial falloff textures: a soft shadow and a soft glow.
 * 128x128 with a smoothstep profile so they still look soft when a single
 * flame blob is magnified across a whole wall. */
static void build_aux_tex(void)
{
  int x, y;
  for (y = 0; y < 128; y++) {
    for (x = 0; x < 128; x++) {
      float dx = (x + 0.5f) / 64.0f - 1.0f;
      float dy = (y + 0.5f) / 64.0f - 1.0f;
      float d = sqrtf(dx * dx + dy * dy);
      float a = 1.0f - d;
      int i = (y * 128 + x) * 4;
      if (a < 0.0f) a = 0.0f;
      a = a * a * (3.0f - 2.0f * a);          /* smoothstep */
      aux_shadow[i + 0] = 0;
      aux_shadow[i + 1] = 0;
      aux_shadow[i + 2] = 0;
      aux_shadow[i + 3] = (unsigned char)(a * 220.0f);
      aux_glow[i + 0] = 255;
      aux_glow[i + 1] = 255;
      aux_glow[i + 2] = 255;
      aux_glow[i + 3] = (unsigned char)(a * 255.0f);
    }
  }
  for (x = 0; x < 8 * 8; x++) {
    white_buf[x * 4 + 0] = 255;
    white_buf[x * 4 + 1] = 255;
    white_buf[x * 4 + 2] = 255;
    white_buf[x * 4 + 3] = 255;
  }
}

static void init_video(void)
{
  /* 4 MB oneshot queue per buffer: a dense frame is a few thousand textured
     quads and the default 1 MB would run out. */
  gs = gsKit_init_global_custom(4 * 1024 * 1024, 256 * 1024);
  gs->PSM = GS_PSM_CT16;
  gs->PSMZ = GS_PSMZ_16S;
  gs->ZBuffering = GS_SETTING_OFF;
  gs->DoubleBuffering = GS_SETTING_ON;
  gs->PrimAlphaEnable = GS_SETTING_ON;
  gs->PrimAAEnable = GS_SETTING_OFF;

  dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
              D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
  dmaKit_chan_init(DMA_CHANNEL_GIF);

  gsKit_init_screen(gs);
  gsKit_mode_switch(gs, GS_ONESHOT);
  gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
  gsKit_set_clamp(gs, GS_CMODE_CLAMP);
  gsKit_set_test(gs, GS_ZTEST_OFF);

  build_aux_tex();
  font_build(font_buf, FONT_TEX_W, FONT_TEX_H);

  make_texture(&tex_mat, materials_atlas, MAT_ATLAS_W, MAT_ATLAS_H);
  make_texture(&tex_shadow, aux_shadow, 128, 128);
  make_texture(&tex_glow, aux_glow, 128, 128);
  make_texture(&tex_font, font_buf, FONT_TEX_W, FONT_TEX_H);
  make_texture(&tex_white, white_buf, 8, 8);

  /* the shadow/glow blobs are magnified a lot, so let them filter */
  tex_shadow.Filter = GS_FILTER_LINEAR;
  tex_glow.Filter = GS_FILTER_LINEAR;
}

/* ------------------------------------------------------------- plat API */

void plat_clear(int r, int g, int b)
{
  gsKit_clear(gs, COL(r, g, b, 128));
}

void plat_tri(const float *sx, const float *sy, const float *u, const float *v,
              int n, int tex, int mat, int r, int g, int b, int a, int add,
              const unsigned char *cv)
{
  GSTEXTURE *t = (tex == 1) ? &tex_shadow : (tex == 2) ? &tex_glow : &tex_mat;
  u64 col = COL(r, g, b, a);
  float tw = (float)t->Width, th = (float)t->Height;
  (void)mat;
  (void)add;

  if (n >= 4) {
    if (cv) {
      /* Gouraud: the GS interpolates the four corner colours across the quad,
         which is exactly the vertex lighting a PS2 scene is built around */
      gsKit_prim_quad_goraud_texture_3d(gs, t,
        sx[0], sy[0], 1, u[0] * tw, v[0] * th,
        sx[1], sy[1], 1, u[1] * tw, v[1] * th,
        sx[2], sy[2], 1, u[2] * tw, v[2] * th,
        sx[3], sy[3], 1, u[3] * tw, v[3] * th,
        COL(cv[0], cv[1], cv[2], a), COL(cv[3], cv[4], cv[5], a),
        COL(cv[6], cv[7], cv[8], a), COL(cv[9], cv[10], cv[11], a));
    } else {
      gsKit_prim_quad_texture_3d(gs, t,
        sx[0], sy[0], 1, u[0] * tw, v[0] * th,
        sx[1], sy[1], 1, u[1] * tw, v[1] * th,
        sx[2], sy[2], 1, u[2] * tw, v[2] * th,
        sx[3], sy[3], 1, u[3] * tw, v[3] * th, col);
    }
  } else if (cv) {
    gsKit_prim_triangle_goraud_texture_3d(gs, t,
      sx[0], sy[0], 1, u[0] * tw, v[0] * th,
      sx[1], sy[1], 1, u[1] * tw, v[1] * th,
      sx[2], sy[2], 1, u[2] * tw, v[2] * th,
      COL(cv[0], cv[1], cv[2], a), COL(cv[3], cv[4], cv[5], a),
      COL(cv[6], cv[7], cv[8], a));
  } else {
    gsKit_prim_triangle_texture_3d(gs, t,
      sx[0], sy[0], 1, u[0] * tw, v[0] * th,
      sx[1], sy[1], 1, u[1] * tw, v[1] * th,
      sx[2], sy[2], 1, u[2] * tw, v[2] * th, col);
  }
}

void plat_rect(float x, float y, float w, float h, int r, int g, int b, int a)
{
  gsKit_prim_sprite_texture(gs, &tex_white, x, y, 0.0f, 0.0f,
                            x + w, y + h, 8.0f, 8.0f, 1, COL(r, g, b, a));
}

void plat_text(float x, float y, float scale, const char *s, int r, int g, int b, int a)
{
  float cx = x;
  u64 col = COL(r, g, b, a);

  while (s && *s) {
    unsigned char c = (unsigned char)*s++;
    int gi, gx, gy;
    if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
    if (c < 32 || c > 126) { cx += 6.0f * scale; continue; }
    gi = c - 32;
    gx = (gi % FONT_COLS) * FONT_CELL_W;
    gy = (gi / FONT_COLS) * FONT_CELL_H;
    gsKit_prim_sprite_texture(gs, &tex_font,
                              cx, y, (float)gx, (float)gy,
                              cx + 5.0f * scale, y + 7.0f * scale,
                              (float)(gx + 5), (float)(gy + 7), 1, col);
    cx += 6.0f * scale;
  }
}

void plat_present(void)
{
  gsKit_queue_exec(gs);
  gsKit_sync_flip(gs);
}

/* ------------------------------------------------------------------- pad */

static unsigned char pad_buf[256] __attribute__((aligned(64)));
static struct padButtonStatus pad_buttons;
static u16 pad_held, pad_prev;
static int pad_inited, pad_ok, pad_reads, pad_tries;
static int pad_mode_done, pad_exec_frames;

static int load_module(unsigned char *mod, unsigned int size, const char *name)
{
  int ret = 0;
  int id = SifExecModuleBuffer(mod, size, 0, NULL, &ret);
  if (id < 0) printf("ashen: %s load failed (%d)\n", name, id);
  return id >= 0;
}

static void pad_wait(void)
{
  volatile int i, k;
  for (k = 0; k < 6; k++)
    for (i = 0; i < 20000; i++) { }
}

static int pad_open_port(void)
{
  if (!pad_inited) {
    if (!padInit(0)) return 0;
    pad_inited = 1;
  }
  if (!padPortOpen(0, 0, pad_buf)) return 0;
  pad_ok = 1;
  return 1;
}

/* Retried from the main loop until the pad answers: which copy of
   sio2man/padman the IOP accepts depends on the BIOS/emulator. */
static int pad_setup(void)
{
  switch (pad_tries++) {
    case 0:
      SifLoadModule("rom0:SIO2MAN", 0, NULL);
      SifLoadModule("rom0:PADMAN", 0, NULL);
      break;
    case 1:
      SifLoadModule("rom0:XSIO2MAN", 0, NULL);
      SifLoadModule("rom0:XPADMAN", 0, NULL);
      break;
    case 2:
      load_module(irx_sio2man, size_irx_sio2man, "sio2man");
      load_module(irx_padman, size_irx_padman, "padman");
      break;
    default:
      break;
  }
  pad_wait();
  return pad_open_port();
}

static void poll_pad(void)
{
  u16 pressed = 0;
  int st = padGetState(0, 0);

  if (st == PAD_STATE_STABLE || st == PAD_STATE_FINDCTP1) {
    if (!pad_mode_done) {
      padSetMainMode(0, 0, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK);
      pad_mode_done = 1;
    }
    pad_exec_frames = 0;
  } else if (st == PAD_STATE_EXECCMD) {
    if (++pad_exec_frames > 90) {
      padSetReqState(0, 0, PAD_RSTAT_COMPLETE);
      pad_mode_done = 1;
      pad_exec_frames = 0;
    }
  } else {
    pad_exec_frames = 0;
  }

  if (padRead(0, 0, &pad_buttons) != 0) {
    pressed = (u16)(0xffff ^ pad_buttons.btns);
    pad_reads++;
  }
  pad_prev = pad_held;
  pad_held = pressed;
}

static int pressed(u16 mask) { return (pad_held & ~pad_prev & mask) != 0; }
static int held(u16 mask) { return (pad_held & mask) != 0; }

static Input in;

static float axis(int v, int dz)
{
  int d = v - 128;
  if (d > dz) return (float)(d - dz) / (float)(127 - dz);
  if (d < -dz) return (float)(d + dz) / (float)(127 - dz);
  return 0.0f;
}

static void build_input(void)
{
  float lx = 0.0f, ly = 0.0f;

  memset(&in, 0, sizeof(in));

  if (held(PAD_LEFT)) lx -= 1.0f;
  if (held(PAD_RIGHT)) lx += 1.0f;
  if (held(PAD_UP)) ly += 1.0f;
  if (held(PAD_DOWN)) ly -= 1.0f;

  if (pad_reads > 0 && (pad_buttons.ljoy_h || pad_buttons.ljoy_v)) {
    float ax = axis((int)pad_buttons.ljoy_h, 26);
    float ay = -axis((int)pad_buttons.ljoy_v, 26);
    if (ax != 0.0f) lx = ax;
    if (ay != 0.0f) ly = ay;
  }
  in.mx = lx;
  in.my = ly;
  in.rx = axis((int)pad_buttons.rjoy_h, 26);
  in.ry = -axis((int)pad_buttons.rjoy_v, 26);

  in.attack = held(PAD_CROSS);
  in.roll = held(PAD_CIRCLE);
  in.lock = held(PAD_L1);
  in.potion = held(PAD_TRIANGLE);
  in.run = held(PAD_R1);
  in.start = held(PAD_START);
  in.back = held(PAD_SELECT);
  (void)pressed;
}

/* ------------------------------------------------------------------ boot */

static void boot_stage(int stage)
{
  u64 c;
  switch (stage) {
    case 1: c = COL(18, 22, 34, 128); break;    /* navy   : video is up */
    case 2: c = COL(84, 22, 104, 128); break;   /* purple : world built */
    default: c = COL(0, 0, 0, 128); break;      /* black  : loop running */
  }
  gsKit_clear(gs, c);
  gsKit_queue_exec(gs);
  gsKit_sync_flip(gs);
}

int main(int argc, char **argv)
{
  int frames = 0, pad_done = 0;

  (void)argc;
  (void)argv;

  SifInitRpc(0);

  init_video();
  boot_stage(1);

  game_init();
  boot_stage(2);

  for (;;) {
    if (!pad_done && frames >= 2) {
      pad_done = 1;
      pad_setup();
    }
    if (frames > 60 && (frames % 60) == 0 && pad_reads == 0 && pad_tries < 24)
      pad_setup();

    poll_pad();
    build_input();
    game_frame(1.0f / 60.0f, &in, 0);
    frames++;
  }

  return 0;
}
