/* util.h - Draw & Roll: shared constants, layout and small helpers.
 *
 * World space is the physics space of pg_phys.h: pixels at the z=0 plane, +y up, the canvas is about
 * x -480..480, y -270..270.  Everything that is drawn "behind" the toys has z < 0, everything that
 * belongs to the player's plane (ramps, balls) lives at z = 0 +- 12.
 */
#ifndef DR_UTIL_H
#define DR_UTIL_H

#include "pg3d.h"
#include "pg_phys.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------ layout */
#define WALL_IN_X      458.0f      /* inner face of the soft cushion walls (physics and look agree) */
#define WALL_T          30.0f      /* half thickness of the wall segments (they extend outwards) */
#define CEIL_Y         330.0f      /* invisible ceiling: balls that are thrown off the top come back */
#define BACK_Z        -120.0f      /* painted backdrop panel */
#define FRONT_Z         46.0f      /* front edge of the side walls */
#define HILL_FRONT_Z    -6.0f      /* hills sit just behind the ramp plane so ramps draw on top of them */
#define HILL_BACK_Z   -112.0f
#define RIM_Y         -168.0f      /* bucket rim height; the hills end exactly here */
#define MOUTH_X        112.0f      /* half width of the bucket mouth = where the hills end */
#define DISP_X           0.0f      /* dispenser cloud, top centre */
#define DISP_Y         188.0f
#define DISP_Z         -34.0f

#define BALL_R_MIN      20.0f
#define BALL_R_MAX      25.0f
#define MAX_BALLS       12
#define STROKE_R        10.0f      /* ramp tube radius = physics half thickness (>= 8) */
#define STROKE_STEP     16.0f      /* resample distance along a drawn line (>= 14) */

enum { TAG_NONE = 0, TAG_BALL, TAG_STROKE, TAG_HILL, TAG_WALL };

/* ------------------------------------------------------------------- math */
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float lerpf(float a, float b, float t)    { return a + (b - a) * t; }
static inline float smooth01(float t)                    { t = clampf(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }
static inline float ease_out_back(float t)               /* 0..1 -> overshoots to ~1.1 and settles at 1 */
{
    t = clampf(t, 0.0f, 1.0f) - 1.0f;
    return 1.0f + t * t * (2.70158f * t + 1.70158f);
}
static inline float decay(float v, float rate, float dt) { return v * expf(-rate * dt); }

static inline PG_Color col_mul(PG_Color c, float k)
{
    float r = c.r * k, g = c.g * k, b = c.b * k;
    return pg_rgba((uint8_t)(r > 255 ? 255 : r), (uint8_t)(g > 255 ? 255 : g), (uint8_t)(b > 255 ? 255 : b), c.a);
}

/* ----------------------------------------------------------------- textures */
/* Upload a painted image (optionally 15-bit dithered, PS1 style) and free it. */
PG_Tex tex_finish(PG_Image *im, bool ps1);
/* A sub-rectangle (in texels of the original image) of a texture returned by tex_finish. */
PG_Tex tex_sub(PG_Tex sheet, int sheet_w, int sheet_h, int x, int y, int w, int h);

/* ------------------------------------------------------------------- meshes */
/* Triangle builder that flips winding to agree with the vertex normals (same trick as the SDK's own). */
typedef struct { PG_Mesh *m; int nv, ni, cap_v, cap_i; } MeshB;
MeshB mb_begin(int max_verts, int max_idx);
int   mb_vert(MeshB *b, PG_V3 p, PG_V3 n, float u, float v, PG_Color c);
void  mb_tri(MeshB *b, int i0, int i1, int i2);
void  mb_quad(MeshB *b, int a, int c, int d, int e);          /* a,c,d,e go around the quad */
PG_Mesh *mb_end(MeshB *b);

/* ------------------------------------------------------------------- sound */
/* All of them are rate limited; "step" is a pentatonic step (0 = C4). */
void snd_note(int step, float vol, float ms);         /* soft bell */
void snd_blip(int step, float vol);                   /* tiny sine blip while drawing */
bool snd_ready(int kind, int min_ticks);              /* generic tick-based rate limiter, kinds 0..15 */

#endif
