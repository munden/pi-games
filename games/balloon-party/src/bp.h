/* bp.h - Balloon Party: declarations shared by the game's modules.
 *
 * Module map (each module owns its own static state; nothing is global):
 *   util.c     camera (a gentle dolly), colour palette, small helpers
 *   paint.c    every texture, painted from nothing (returns PG_Image, caller uploads)
 *   balloon.c  balloon meshes, buoyancy simulation, strings, picking, popping, batting
 *   room.c     the party room: wall, windows, bunting, floor, presents, sunbeams, dust
 *   fx.c       confetti, sparkles, prizes, the hint hand, touch glows
 *   snd.c      gentle sound governor (a child will pop ten balloons at once)
 *   main.c     PG_Game glue and the touch rules
 */
#ifndef BP_H
#define BP_H

#include "pg3d.h"

/* ------------------------------------------------------------------ tuning */
#define MAX_BALLOONS   28
#define MIN_BALLOONS   10
#define N_COL          8
#define CAM_FOV        46.0f
#define WALL_Z         (-330.0f)
#define FLOOR_Y        (-213.0f)
#define STR_N          6                  /* verlet points per balloon string */

#define BAT_SPEED      250.0f             /* px/s: faster than this is a swipe (bat), slower is a touch (pop) */
#define HIT_PAD        16.0f              /* px of forgiveness around a balloon */
#define LIGHT_X        (-0.42f)           /* direction towards the key light (the sunny window is on the left) */
#define LIGHT_Y        0.62f
#define LIGHT_Z        0.66f

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float smooth01(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }
static inline float frand(float lo, float hi) { return pg_rand_range(lo, hi); }

/* ------------------------------------------------------------------- util.c */
typedef struct { PG_Camera pg; float ex, ey, dist, focal; } Cam;
void       cam_update(float t);                                  /* once per tick */
const Cam *cam_get(void);
bool       cam_project(float x, float y, float z, float *sx, float *sy);
void       cam_unproject(float sx, float sy, float z, float *x, float *y);
float      cam_scale(float z);                                   /* screen px per world unit at depth z */
PG_Tex     tex_upload(PG_Image *im, bool dither);                /* atlas upload, frees the image */

extern const uint32_t BAL_RGB[N_COL];                            /* balloon colours, one per pentatonic note */
PG_Color   bal_color(int col, uint8_t a);

/* ------------------------------------------------------------------ paint.c */
PG_Image *paint_balloon(void);
PG_Image *paint_glint(void);
PG_Image *paint_sparkle(void);
PG_Image *paint_ring(void);
PG_Image *paint_wall(void);
PG_Image *paint_trim(void);
PG_Image *paint_wainscot(void);
PG_Image *paint_floor(void);
PG_Image *paint_rug(void);
PG_Image *paint_window(int which);
PG_Image *paint_frame(void);
PG_Image *paint_gift(uint32_t paper, uint32_t ribbon, int style);
PG_Image *paint_bow(uint32_t ribbon);
PG_Image *paint_flags(void);
PG_Image *paint_twine(void);
PG_Image *paint_hand(void);
PG_Image *paint_prize(int kind);                                 /* 0 star, 1 heart, 2 note */
PG_Image *paint_rainbow(void);
PG_Image *paint_streamer(uint32_t a, uint32_t b);

/* each of these is "(0,0)-(1,1)" over the texture; helper for sprites that want a sub-rectangle */

/* ---------------------------------------------------------------- balloon.c */
void  balloons_init(void);
void  balloons_free(void);
void  balloons_update(float t, int idle_boost);
void  balloons_draw_shadows(void);
void  balloons_draw(void);
int   balloons_count(void);
int   balloon_hit(float sx, float sy, float pad);                /* frontmost balloon under a screen point, or -1 */
bool  balloon_contains(int i, float sx, float sy, float pad);
bool  balloon_swept(int i, float x0, float y0, float x1, float y1, float pad);
uint32_t balloon_gen(int i);
bool  balloon_alive(int i, uint32_t gen);
bool  balloon_screen(int i, float *sx, float *sy, float *sr);    /* centre and radius in canvas px */
int   balloon_for_hint(void);
void  balloon_poke(int i, float sx, float sy);                   /* instant visual dent when touched */
void  balloon_pop(int i);
void  balloon_bat(int i, float svx, float svy);                  /* screen-space velocity of the finger, px/s */
void  balloons_puff(float sx, float sy);                         /* a tap in empty air nudges neighbours */
int   balloon_slots(void);

/* ------------------------------------------------------------------- room.c */
void  room_init(void);
void  room_free(void);
void  room_update(float t);
void  room_draw_wall(void);                                      /* BG: wall, windows, trim (before the wall shadows) */
void  room_draw_floor(void);                                     /* BG: bunting, floor, rug, presents (after the shadows) */
void  room_draw_ambient(void);                                   /* additive light shafts and dust */

/* --------------------------------------------------------------------- fx.c */
void  fx_init(void);
void  fx_free(void);
void  fx_update(void);
void  fx_draw(void);
void  fx_pop(PG_V3 p, int col, float r);
void  fx_burst(PG_V3 p, float hue, int n, float speed);
void  fx_trail(PG_V3 p, float hue);
void  fx_ripple(PG_V3 p, float size, PG_Color c);
void  fx_prize(PG_V3 p);                                         /* maybe release a surprise (random kind) */
int   fx_prize_hit(float sx, float sy);
void  fx_prize_pop(int i);
void  fx_touch_glow(const PG_Touch *t);                          /* soft light under a finger */
void  hint_update(float idle_s, bool first_time);
void  hint_draw(void);

/* ------------------------------------------------------------------- snd.c */
void  snd_update(void);
void  snd_pop(int col, float sizek);
void  snd_bat(float sizek, float strength);
void  snd_bump(float impact);
void  snd_tap(void);
void  snd_prize(void);

#endif /* BP_H */
