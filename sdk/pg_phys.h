/* pg_phys.h - small 2D rigid-body physics: circles, boxes, static line segments.
 *
 * Units are canvas pixels and seconds, +y is UP (same as pg3d.h). Typical gravity is (0, -900).
 * The solver is a sequential-impulse engine with warm starting (box stacking and the box-box
 * clipping follow Erin Catto's Box2D-Lite, MIT licence; circles, segments, restitution, the
 * grab spring and the contact events are ours). It needs no SDL and allocates once.
 *
 * Shapes:  CIRCLE (radius), BOX (half extents hx,hy, rotated by angle),
 *          SEGMENT (static thick line, collides with CIRCLES only - use it for drawn ramps).
 * Static bodies have density 0 (inv_mass 0). A "kinematic" body is a static body whose vx,vy you
 * set every tick: it shoves dynamic bodies (finger paddles) but is never pushed back.
 *
 * Budget: ~300 dynamic bodies at 60 Hz with the default 2 substeps is fine on the Pi Zero 2 W
 * (broad phase is O(n^2) on bounding circles). Keep shapes >= ~10 px: smaller, fast bodies can tunnel.
 */
#ifndef PG_PHYS_H
#define PG_PHYS_H

#include <stdbool.h>
#include <stdint.h>

#define PG_PHYS_MAX_BODIES 512

typedef enum { PG_BODY_CIRCLE, PG_BODY_BOX, PG_BODY_SEGMENT } PG_BodyKind;

typedef struct PG_Body {
    bool        alive;
    PG_BodyKind kind;
    float x, y, angle;              /* centre, rotation in radians (counter-clockwise, y up) */
    float vx, vy, w;                /* velocity px/s, angular velocity rad/s */
    float inv_mass, inv_inertia;    /* 0 = static / kinematic */
    float friction;                 /* default 0.5 */
    float restitution;              /* bounciness 0..1, default 0.25 */
    float radius;                   /* CIRCLE radius; SEGMENT half-thickness */
    float hx, hy;                   /* BOX half extents */
    float ax, ay, bx, by;           /* SEGMENT end points (world) */
    float lin_damp, ang_damp;       /* per second, defaults 0.05 / 0.2 */
    int   tag;                      /* yours */
    void *user;                     /* yours */
    /* private */
    float fx, fy, torque, c, s;
} PG_Body;

/* Called once per new touching pair after pg_world_step. impact_speed is the approach speed
 * (px/s) along the contact normal, x,y the contact point. Use it for sounds; throttle in the game. */
typedef void (*PG_ContactFn)(PG_Body *a, PG_Body *b, float impact_speed, float x, float y, void *ud);

typedef struct PG_World PG_World;

PG_World *pg_world_new(float gx, float gy);
void      pg_world_free(PG_World *w);
void      pg_world_clear(PG_World *w);                         /* remove every body */
void      pg_world_set_gravity(PG_World *w, float gx, float gy);
void      pg_world_set_quality(PG_World *w, int substeps, int iterations);   /* default 2, 12 */
void      pg_world_set_contact_fn(PG_World *w, PG_ContactFn fn, void *ud);

/* density 0 => static. All return NULL when the world is full (PG_PHYS_MAX_BODIES). */
PG_Body *pg_world_add_circle(PG_World *w, float x, float y, float radius, float density);
PG_Body *pg_world_add_box(PG_World *w, float x, float y, float hx, float hy, float angle, float density);
PG_Body *pg_world_add_segment(PG_World *w, float ax, float ay, float bx, float by, float half_thickness);
void     pg_world_remove(PG_World *w, PG_Body *b);

void     pg_world_step(PG_World *w, float dt);                 /* call once per update tick with PG_DT */
int      pg_world_capacity(const PG_World *w);                 /* PG_PHYS_MAX_BODIES */
PG_Body *pg_world_body(PG_World *w, int index);                /* iterate 0..capacity-1, skip !alive */
int      pg_world_count(const PG_World *w);                    /* alive bodies */

/* helpers */
bool     pg_body_contains(const PG_Body *b, float x, float y);                    /* point inside (circle/box) */
PG_Body *pg_world_pick(PG_World *w, float x, float y);                            /* topmost dynamic body at a point */
void     pg_body_local_to_world(const PG_Body *b, float lx, float ly, float *wx, float *wy);
void     pg_body_world_to_local(const PG_Body *b, float wx, float wy, float *lx, float *ly);
void     pg_body_apply_impulse(PG_Body *b, float ix, float iy, float wx, float wy);   /* impulse at a world point */
void     pg_body_apply_force(PG_Body *b, float fx, float fy);                      /* at the centre, cleared each step */
float    pg_body_speed(const PG_Body *b);

/* Drag a body by a point on it with a soft spring (a finger holding a block).
 * (lx,ly) is the grab point in the body's local frame (from pg_body_world_to_local at touch-down),
 * (tx,ty) the world target (the finger). Call every tick while held. freq ~ 5 Hz, damping ~ 0.8. */
void     pg_body_grab_step(PG_Body *b, float lx, float ly, float tx, float ty, float freq_hz, float damping, float dt);

#endif /* PG_PHYS_H */
