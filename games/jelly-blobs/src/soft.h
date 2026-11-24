/* soft.h - soft-body jelly physics for Jelly Blobs.  Plain C, no SDL: it can be tested stand-alone.
 *
 * Every blob is a ring of SB_N point masses plus one belly particle, integrated with position based
 * dynamics (Verlet with constraints) in 2 substeps per 60 Hz tick:
 *   - shape matching pulls every particle toward the rest shape (only a fraction of the rotation is
 *     followed, so faces stay upright and blobs can never turn inside out),
 *   - an area ("pressure") constraint keeps the volume: squash a blob and it bulges,
 *   - soft edge limits stop the skin from over-stretching or collapsing, and a curvature-aware smoothing removes
 *     spikes and folds without touching the rest shape or gentle wobbles,
 *   - floor / walls / ceiling with friction, blob-vs-blob collision (circle repulsion between the
 *     centres plus boundary points pushed out of the neighbour's polygon).
 * Fingers are damped springs on the particles around the touched point; all forces, speeds and
 * corrections are clamped, and a blob that ever looks broken (NaN, inverted, huge) heals itself.
 *
 * World units are canvas pixels at the z=0 plane, origin at the canvas centre, +y up. */
#ifndef JB_SOFT_H
#define JB_SOFT_H

#include <stdbool.h>

#define SB_N        16                  /* ring particles per blob */
#define SB_NP       (SB_N + 1)
#define SB_C        SB_N                /* index of the belly (centre) particle */
#define SB_MAX      6
#define SB_FINGERS  17                  /* 16 touch contacts + the hint hand */

#define SB_WALL_X   452.0f
#define SB_FLOOR_Y  (-215.0f)
#define SB_CEIL_Y   250.0f

typedef enum { SE_LAND, SE_WALL, SE_BUMP, SE_GRAB, SE_RELEASE, SE_HEAL } SoftEventKind;
typedef struct { SoftEventKind kind; int a, b; float speed, x, y; } SoftEvent;

typedef struct {
    /* particles; [SB_C] is the belly */
    float x[SB_NP], y[SB_NP], vx[SB_NP], vy[SB_NP];
    float ox[SB_NP], oy[SB_NP], vpy[SB_NP], vpx[SB_NP];     /* step scratch: previous position / pre-contact velocity */
    unsigned char cf[SB_NP];                                /* contact flags of the current substep */
    float qx[SB_NP], qy[SB_NP];                              /* rest offsets from the centroid (px) */
    float edge0[SB_N];                                       /* rest edge lengths */
    float bend0[SB_N];                                       /* rest curvature of the skin at each particle (px) */
    float radius;                                            /* equivalent radius: rest area = pi * radius^2 */
    float area0, foot;                                       /* rest area, rest height of the centroid above the floor */
    float wrest, hrest;                                      /* rest width and height */
    float alpha;                                             /* shape stiffness per iteration */

    /* controls (set by the game) */
    float ell_extra;                                         /* extra log stretch: >0 tall, <0 squat (decays) */
    float ell_bias;                                          /* steady log stretch, e.g. breathing (no decay) */
    float breath;                                            /* area multiplier offset, e.g. 0.02 */
    float stretch_k;                                         /* scale of velocity stretch (1 = normal) */

    /* derived once per tick (read only for the game) */
    float cx, cy, vcx, vcy;                                  /* centroid and its velocity */
    float ravg, rmax, area;
    float minx, maxx, miny, maxy;
    float tilt;                                              /* smoothed shape rotation, radians */
    float sx, sy, ell;                                       /* current goal stretch (sy = e^ell, sx = e^-ell) */
    float theta_raw;                                         /* last measured shape rotation */
    int   floor_acc;
    float stretch;                                           /* 0..1: how far the skin is pulled from the rest shape */
    float lowest;                                            /* height of the lowest particle above the floor */
    int   held;                                              /* fingers currently holding it */
    int   floor_n;                                           /* particles that touched the floor last tick */
    int   air_ticks;                                         /* ticks since the floor was touched */
    int   wall_cool, bad_ticks;
    float land_boost;                                        /* impact speed waiting to be turned into a rebound */
    int   land_age;
    bool  grounded;
} SoftBlob;

typedef struct {
    bool  active;
    int   blob;
    float fx, fy, fvx, fvy;                                  /* finger position and velocity (world) */
    float w[SB_NP];                                          /* how strongly each particle is pulled */
    float ox[SB_NP], oy[SB_NP];                              /* anchor offsets from the finger */
} SoftGrab;

typedef struct {
    SoftBlob b[SB_MAX];
    int      n;
    SoftGrab g[SB_FINGERS];
    SoftEvent ev[48];
    int      nev;
    int      bump_cool[SB_MAX][SB_MAX];
    int      pair_touch[SB_MAX][SB_MAX];
    unsigned ticks;
} SoftWorld;

void  soft_init(SoftWorld *w, int n, const float *radius, const float *x, const float *y);
void  soft_step(SoftWorld *w);                               /* one 1/60 s tick (2 substeps) */

/* picking: which blob is under (x,y)?  slop = extra reach in px.  Returns -1 for none. */
int   soft_pick(const SoftWorld *w, float x, float y, float slop);
/* distance from the blob centroid to its skin along the direction of (x,y): the local radius */
float soft_local_radius(const SoftBlob *b, float dx, float dy);

int   soft_grab(SoftWorld *w, int finger, int blob, float x, float y);     /* start holding; returns the blob */
void  soft_move(SoftWorld *w, int finger, float x, float y, float vx, float vy);
void  soft_release(SoftWorld *w, int finger);

void  soft_poke(SoftWorld *w, int blob, float x, float y, float speed);  /* push the skin in at a point */
void  soft_hop(SoftWorld *w, int blob, float up, float side);            /* give it a jump: up / sideways speed, px/s */
void  soft_slap(SoftWorld *w, float x, float y, float range, float up);  /* the mat is slapped: blobs near it hop */

int   soft_events(SoftWorld *w, const SoftEvent **out);                  /* events since the last call; clears them */

#endif /* JB_SOFT_H */
