/* pg_phys.c - 2D rigid bodies. See pg_phys.h.
 *
 * Sequential impulses with warm starting and Baumgarte position bias, after Box2D-Lite
 * (Erin Catto, MIT licence): the box-box SAT + clipping and the arbiter/contact persistence
 * follow that design. Added here: circles, thick static segments, restitution, kinematic
 * bodies, sub-stepping, impact events and a grab spring.
 */
#include "pg_phys.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ARB      3072
#define MAX_EVENTS   128
#ifndef SLOP
#define SLOP         0.5f
#endif
#ifndef BIAS_FACTOR
#define BIAS_FACTOR  0.2f
#endif
#define MAX_BIAS     300.0f
#define BOUNCE_MIN   90.0f       /* px/s approach speed below which there is no bounce */
#define EVENT_MIN    45.0f
#define MAX_SPEED    3500.0f
#define MAX_SPIN     30.0f
#define PI_F         3.14159265358979f

typedef struct { float x, y; } V2;
static inline V2    v2(float x, float y) { V2 v = { x, y }; return v; }
static inline V2    vadd(V2 a, V2 b) { return v2(a.x + b.x, a.y + b.y); }
static inline V2    vsub(V2 a, V2 b) { return v2(a.x - b.x, a.y - b.y); }
static inline V2    vmul(V2 a, float k) { return v2(a.x * k, a.y * k); }
static inline float vdot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

typedef struct {
    float nx, ny, px, py, sep;
    uint32_t feature;
    float pn, pt, mass_n, mass_t, bias, r1x, r1y, r2x, r2y;
} Contact;

typedef struct { int ia, ib, n; bool fresh; float friction, restitution; Contact c[2]; } Arbiter;
typedef struct { PG_Body *a, *b; float speed, x, y; } Event;

struct PG_World {
    PG_Body bodies[PG_PHYS_MAX_BODIES];
    int   high;
    float gx, gy;
    int   substeps, iterations;
    Arbiter *cur, *prev;
    int   ncur, nprev;
    PG_ContactFn fn; void *ud;
    Event events[MAX_EVENTS];
    int   nevents;
};

/* -------------------------------------------------------------- world */
PG_World *pg_world_new(float gx, float gy)
{
    PG_World *w = calloc(1, sizeof *w);
    if (!w) return NULL;
    w->cur = malloc(sizeof(Arbiter) * MAX_ARB);
    w->prev = malloc(sizeof(Arbiter) * MAX_ARB);
    if (!w->cur || !w->prev) { free(w->cur); free(w->prev); free(w); return NULL; }
    w->gx = gx; w->gy = gy; w->substeps = 2; w->iterations = 12;
    return w;
}

void pg_world_free(PG_World *w) { if (w) { free(w->cur); free(w->prev); free(w); } }
void pg_world_clear(PG_World *w) { memset(w->bodies, 0, sizeof w->bodies); w->high = 0; w->ncur = w->nprev = 0; w->nevents = 0; }
void pg_world_set_gravity(PG_World *w, float gx, float gy) { w->gx = gx; w->gy = gy; }
void pg_world_set_quality(PG_World *w, int sub, int it) { w->substeps = sub < 1 ? 1 : sub > 8 ? 8 : sub; w->iterations = it < 1 ? 1 : it > 30 ? 30 : it; }
void pg_world_set_contact_fn(PG_World *w, PG_ContactFn fn, void *ud) { w->fn = fn; w->ud = ud; }
int  pg_world_capacity(const PG_World *w) { (void)w; return PG_PHYS_MAX_BODIES; }
PG_Body *pg_world_body(PG_World *w, int i) { return (i >= 0 && i < PG_PHYS_MAX_BODIES) ? &w->bodies[i] : NULL; }
int  pg_world_count(const PG_World *w) { int n = 0; for (int i = 0; i < w->high; i++) n += w->bodies[i].alive; return n; }

static PG_Body *alloc_body(PG_World *w)
{
    for (int i = 0; i < PG_PHYS_MAX_BODIES; i++) {
        if (w->bodies[i].alive) continue;
        if (i >= w->high) w->high = i + 1;
        PG_Body *b = &w->bodies[i];
        memset(b, 0, sizeof *b);
        b->alive = true; b->friction = 0.5f; b->restitution = 0.25f; b->lin_damp = 0.05f; b->ang_damp = 0.2f; b->c = 1.0f;
        return b;
    }
    return NULL;
}

PG_Body *pg_world_add_circle(PG_World *w, float x, float y, float r, float density)
{
    PG_Body *b = alloc_body(w);
    if (!b) return NULL;
    b->kind = PG_BODY_CIRCLE; b->x = x; b->y = y; b->radius = r;
    if (density > 0) { float m = density * PI_F * r * r; b->inv_mass = 1.0f / m; b->inv_inertia = 1.0f / (0.5f * m * r * r); }
    return b;
}

PG_Body *pg_world_add_box(PG_World *w, float x, float y, float hx, float hy, float angle, float density)
{
    PG_Body *b = alloc_body(w);
    if (!b) return NULL;
    b->kind = PG_BODY_BOX; b->x = x; b->y = y; b->hx = hx; b->hy = hy; b->angle = angle;
    b->c = cosf(angle); b->s = sinf(angle);
    if (density > 0) { float m = density * 4 * hx * hy; b->inv_mass = 1.0f / m; b->inv_inertia = 1.0f / (m * (4 * hx * hx + 4 * hy * hy) / 12.0f); }
    return b;
}

PG_Body *pg_world_add_segment(PG_World *w, float ax, float ay, float bx, float by, float half_thickness)
{
    PG_Body *b = alloc_body(w);
    if (!b) return NULL;
    b->kind = PG_BODY_SEGMENT; b->ax = ax; b->ay = ay; b->bx = bx; b->by = by; b->radius = half_thickness;
    b->x = (ax + bx) * 0.5f; b->y = (ay + by) * 0.5f;
    b->friction = 0.35f;
    return b;
}

void pg_world_remove(PG_World *w, PG_Body *b) { (void)w; if (b) b->alive = false; }

/* ------------------------------------------------------- collision: shapes */
static int collide_circles(const PG_Body *A, const PG_Body *B, Contact *o)
{
    float dx = B->x - A->x, dy = B->y - A->y, rs = A->radius + B->radius, d2 = dx * dx + dy * dy;
    if (d2 >= rs * rs) return 0;
    float d = sqrtf(d2), nx, ny;
    if (d > 1e-5f) { nx = dx / d; ny = dy / d; } else { nx = 0; ny = 1; d = 0; }
    o->nx = nx; o->ny = ny; o->sep = d - rs; o->feature = 0x01;
    o->px = A->x + nx * (A->radius + o->sep * 0.5f); o->py = A->y + ny * (A->radius + o->sep * 0.5f);
    return 1;
}

/* normal points from the box X to the circle C */
static int circle_box(const PG_Body *C, const PG_Body *X, Contact *o)
{
    float dx = C->x - X->x, dy = C->y - X->y;
    float lx = X->c * dx + X->s * dy, ly = -X->s * dx + X->c * dy;
    float cx = clampf(lx, -X->hx, X->hx), cy = clampf(ly, -X->hy, X->hy);
    float ex = lx - cx, ey = ly - cy, d2 = ex * ex + ey * ey;
    float nlx, nly, sep, plx, ply;
    if (d2 > 1e-10f) {
        float d = sqrtf(d2);
        if (d >= C->radius) return 0;
        nlx = ex / d; nly = ey / d; sep = d - C->radius; plx = cx; ply = cy;
    } else {
        float px = X->hx - fabsf(lx), py = X->hy - fabsf(ly);
        if (px < py) { nlx = lx >= 0 ? 1.0f : -1.0f; nly = 0; sep = -(px + C->radius); plx = lx >= 0 ? X->hx : -X->hx; ply = ly; }
        else         { nlx = 0; nly = ly >= 0 ? 1.0f : -1.0f; sep = -(py + C->radius); plx = lx; ply = ly >= 0 ? X->hy : -X->hy; }
    }
    o->nx = X->c * nlx - X->s * nly; o->ny = X->s * nlx + X->c * nly;
    o->px = X->x + X->c * plx - X->s * ply; o->py = X->y + X->s * plx + X->c * ply;
    o->sep = sep; o->feature = 0x02;
    return 1;
}

/* normal points from the segment S to the circle C */
static int circle_segment(const PG_Body *C, const PG_Body *S, Contact *o)
{
    float abx = S->bx - S->ax, aby = S->by - S->ay, len2 = abx * abx + aby * aby;
    float t = len2 > 0 ? clampf(((C->x - S->ax) * abx + (C->y - S->ay) * aby) / len2, 0, 1) : 0;
    float qx = S->ax + abx * t, qy = S->ay + aby * t;
    float dx = C->x - qx, dy = C->y - qy, rr = C->radius + S->radius, d2 = dx * dx + dy * dy;
    if (d2 >= rr * rr) return 0;
    float nx, ny, d = sqrtf(d2);
    if (d > 1e-5f) { nx = dx / d; ny = dy / d; }
    else { float l = sqrtf(len2); if (l < 1e-6f) { nx = 0; ny = 1; } else { nx = -aby / l; ny = abx / l; } d = 0; }
    o->nx = nx; o->ny = ny; o->sep = d - rr; o->feature = 0x03;
    o->px = qx + nx * S->radius; o->py = qy + ny * S->radius;
    return 1;
}

/* ---- box vs box: SAT + clipping (Box2D-Lite) ---- */
enum { NO_EDGE = 0, EDGE1, EDGE2, EDGE3, EDGE4 };
enum { FACE_A_X, FACE_A_Y, FACE_B_X, FACE_B_Y };
typedef struct { V2 v; uint32_t fp; } ClipV;       /* fp bytes: inEdge1, outEdge1, inEdge2, outEdge2 */
#define FP(in1, out1, in2, out2) ((uint32_t)(in1) | ((uint32_t)(out1) << 8) | ((uint32_t)(in2) << 16) | ((uint32_t)(out2) << 24))
static uint32_t fp_flip(uint32_t f) { return ((f & 0xFF) << 16) | (f & 0xFF0000u) >> 16 | ((f & 0xFF00) << 16) | ((f >> 16) & 0xFF00u) | 0; }
static uint32_t fp_set(uint32_t f, int byte, uint32_t val) { f &= ~(0xFFu << (8 * byte)); return f | (val << (8 * byte)); }

static int clip_segment(ClipV out[2], const ClipV in[2], V2 n, float offset, int clip_edge)
{
    int k = 0;
    float d0 = vdot(n, in[0].v) - offset, d1 = vdot(n, in[1].v) - offset;
    if (d0 <= 0) out[k++] = in[0];
    if (d1 <= 0) out[k++] = in[1];
    if (d0 * d1 < 0) {
        float t = d0 / (d0 - d1);
        out[k].v = vadd(in[0].v, vmul(vsub(in[1].v, in[0].v), t));
        if (d0 > 0) { out[k].fp = fp_set(fp_set(in[0].fp, 0, (uint32_t)clip_edge), 2, NO_EDGE); }
        else        { out[k].fp = fp_set(fp_set(in[1].fp, 1, (uint32_t)clip_edge), 3, NO_EDGE); }
        k++;
    }
    return k;
}

static void incident_edge(ClipV c[2], V2 h, V2 pos, V2 r1, V2 r2, V2 normal)
{
    /* the normal is from the reference box; express it in the incident box frame and flip it */
    V2 n = v2(-vdot(r1, normal), -vdot(r2, normal));
    V2 na = v2(fabsf(n.x), fabsf(n.y));
    if (na.x > na.y) {
        if (n.x > 0) { c[0].v = v2(h.x, -h.y); c[0].fp = FP(0, 0, EDGE3, EDGE4); c[1].v = v2(h.x, h.y); c[1].fp = FP(0, 0, EDGE4, EDGE1); }
        else         { c[0].v = v2(-h.x, h.y); c[0].fp = FP(0, 0, EDGE1, EDGE2); c[1].v = v2(-h.x, -h.y); c[1].fp = FP(0, 0, EDGE2, EDGE3); }
    } else {
        if (n.y > 0) { c[0].v = v2(h.x, h.y); c[0].fp = FP(0, 0, EDGE4, EDGE1); c[1].v = v2(-h.x, h.y); c[1].fp = FP(0, 0, EDGE1, EDGE2); }
        else         { c[0].v = v2(-h.x, -h.y); c[0].fp = FP(0, 0, EDGE2, EDGE3); c[1].v = v2(h.x, -h.y); c[1].fp = FP(0, 0, EDGE3, EDGE4); }
    }
    for (int i = 0; i < 2; i++) c[i].v = vadd(pos, v2(r1.x * c[i].v.x + r2.x * c[i].v.y, r1.y * c[i].v.x + r2.y * c[i].v.y));
}

static int collide_boxes(const PG_Body *A, const PG_Body *B, Contact *out)
{
    V2 hA = v2(A->hx, A->hy), hB = v2(B->hx, B->hy), posA = v2(A->x, A->y), posB = v2(B->x, B->y);
    V2 a1 = v2(A->c, A->s), a2 = v2(-A->s, A->c), b1 = v2(B->c, B->s), b2 = v2(-B->s, B->c);   /* rotation columns */
    V2 dp = vsub(posB, posA);
    V2 dA = v2(vdot(a1, dp), vdot(a2, dp)), dB = v2(vdot(b1, dp), vdot(b2, dp));
    float c11 = fabsf(vdot(a1, b1)), c21 = fabsf(vdot(a2, b1)), c12 = fabsf(vdot(a1, b2)), c22 = fabsf(vdot(a2, b2));
    V2 faceA = v2(fabsf(dA.x) - hA.x - (c11 * hB.x + c12 * hB.y), fabsf(dA.y) - hA.y - (c21 * hB.x + c22 * hB.y));
    if (faceA.x > 0 || faceA.y > 0) return 0;
    V2 faceB = v2(fabsf(dB.x) - (c11 * hA.x + c21 * hA.y) - hB.x, fabsf(dB.y) - (c12 * hA.x + c22 * hA.y) - hB.y);
    if (faceB.x > 0 || faceB.y > 0) return 0;

    int axis = FACE_A_X; float sep = faceA.x; V2 normal = dA.x > 0 ? a1 : vmul(a1, -1);
    const float rel = 0.95f, abs_ = 0.01f;
    if (faceA.y > rel * sep + abs_ * hA.y) { axis = FACE_A_Y; sep = faceA.y; normal = dA.y > 0 ? a2 : vmul(a2, -1); }
    if (faceB.x > rel * sep + abs_ * hB.x) { axis = FACE_B_X; sep = faceB.x; normal = dB.x > 0 ? b1 : vmul(b1, -1); }
    if (faceB.y > rel * sep + abs_ * hB.y) { axis = FACE_B_Y; sep = faceB.y; normal = dB.y > 0 ? b2 : vmul(b2, -1); }

    V2 front_n, side_n; ClipV inc[2]; float front, neg_side, pos_side; int neg_edge, pos_edge;
    switch (axis) {
    case FACE_A_X:
        front_n = normal; front = vdot(posA, front_n) + hA.x; side_n = a2;
        { float s = vdot(posA, side_n); neg_side = -s + hA.y; pos_side = s + hA.y; }
        neg_edge = EDGE3; pos_edge = EDGE1; incident_edge(inc, hB, posB, b1, b2, front_n); break;
    case FACE_A_Y:
        front_n = normal; front = vdot(posA, front_n) + hA.y; side_n = a1;
        { float s = vdot(posA, side_n); neg_side = -s + hA.x; pos_side = s + hA.x; }
        neg_edge = EDGE2; pos_edge = EDGE4; incident_edge(inc, hB, posB, b1, b2, front_n); break;
    case FACE_B_X:
        front_n = vmul(normal, -1); front = vdot(posB, front_n) + hB.x; side_n = b2;
        { float s = vdot(posB, side_n); neg_side = -s + hB.y; pos_side = s + hB.y; }
        neg_edge = EDGE3; pos_edge = EDGE1; incident_edge(inc, hA, posA, a1, a2, front_n); break;
    default:
        front_n = vmul(normal, -1); front = vdot(posB, front_n) + hB.y; side_n = b1;
        { float s = vdot(posB, side_n); neg_side = -s + hB.x; pos_side = s + hB.x; }
        neg_edge = EDGE2; pos_edge = EDGE4; incident_edge(inc, hA, posA, a1, a2, front_n); break;
    }
    ClipV c1[2], c2[2];
    if (clip_segment(c1, inc, vmul(side_n, -1), neg_side, neg_edge) < 2) return 0;
    if (clip_segment(c2, c1, side_n, pos_side, pos_edge) < 2) return 0;

    int n = 0;
    for (int i = 0; i < 2; i++) {
        float s = vdot(front_n, c2[i].v) - front;
        if (s > 0) continue;
        out[n].sep = s; out[n].nx = normal.x; out[n].ny = normal.y;
        out[n].px = c2[i].v.x - s * front_n.x; out[n].py = c2[i].v.y - s * front_n.y;
        out[n].feature = (axis == FACE_B_X || axis == FACE_B_Y) ? fp_flip(c2[i].fp) : c2[i].fp;
        n++;
    }
    return n;
}

/* normal points from A to B */
static int collide(const PG_Body *A, const PG_Body *B, Contact *o)
{
    int n;
    switch (A->kind) {
    case PG_BODY_CIRCLE:
        if (B->kind == PG_BODY_CIRCLE) return collide_circles(A, B, o);
        if (B->kind == PG_BODY_BOX)    { n = circle_box(A, B, o);     if (n) { o->nx = -o->nx; o->ny = -o->ny; } return n; }
        n = circle_segment(A, B, o);   if (n) { o->nx = -o->nx; o->ny = -o->ny; } return n;
    case PG_BODY_BOX:
        if (B->kind == PG_BODY_BOX)    return collide_boxes(A, B, o);
        if (B->kind == PG_BODY_CIRCLE) return circle_box(B, A, o);
        return 0;
    default:
        if (B->kind == PG_BODY_CIRCLE) return circle_segment(B, A, o);
        return 0;
    }
}

/* ---------------------------------------------------------------- solver */
static float bound_r(const PG_Body *b)
{
    switch (b->kind) {
    case PG_BODY_CIRCLE: return b->radius;
    case PG_BODY_BOX:    return sqrtf(b->hx * b->hx + b->hy * b->hy);
    default: { float dx = b->bx - b->ax, dy = b->by - b->ay; return 0.5f * sqrtf(dx * dx + dy * dy) + b->radius; }
    }
}

static void broadphase(PG_World *w)
{
    static float br[PG_PHYS_MAX_BODIES];
    int np = 0;
    for (int i = 0; i < w->high; i++) br[i] = w->bodies[i].alive ? bound_r(&w->bodies[i]) : 0.0f;
    int p = 0;
    w->ncur = 0;
    for (int i = 0; i < w->high; i++) {
        PG_Body *a = &w->bodies[i];
        if (!a->alive) continue;
        for (int j = i + 1; j < w->high; j++) {
            PG_Body *b = &w->bodies[j];
            if (!b->alive) continue;
            if (a->inv_mass == 0 && b->inv_mass == 0) continue;
            float dx = b->x - a->x, dy = b->y - a->y, rs = br[i] + br[j];
            if (dx * dx + dy * dy > rs * rs) continue;
            Contact c[2];
            int n = collide(a, b, c);
            if (n == 0 || w->ncur >= MAX_ARB) { if (n && w->ncur >= MAX_ARB) np++; continue; }
            Arbiter *ar = &w->cur[w->ncur++];
            ar->ia = i; ar->ib = j; ar->n = n; ar->fresh = true;
            ar->friction = sqrtf(a->friction * b->friction);
            ar->restitution = a->restitution > b->restitution ? a->restitution : b->restitution;
            for (int k = 0; k < n; k++) { ar->c[k] = c[k]; ar->c[k].pn = ar->c[k].pt = 0; }
            while (p < w->nprev && (w->prev[p].ia < i || (w->prev[p].ia == i && w->prev[p].ib < j))) p++;
            if (p < w->nprev && w->prev[p].ia == i && w->prev[p].ib == j) {
                ar->fresh = false;
                for (int k = 0; k < n; k++)
                    for (int q = 0; q < w->prev[p].n; q++)
                        if (w->prev[p].c[q].feature == ar->c[k].feature) { ar->c[k].pn = w->prev[p].c[q].pn; ar->c[k].pt = w->prev[p].c[q].pt; break; }
            }
        }
    }
    (void)np;
}

static inline void apply_imp(PG_Body *a, PG_Body *b, float px, float py, float r1x, float r1y, float r2x, float r2y)
{
    a->vx -= a->inv_mass * px; a->vy -= a->inv_mass * py; a->w -= a->inv_inertia * (r1x * py - r1y * px);
    b->vx += b->inv_mass * px; b->vy += b->inv_mass * py; b->w += b->inv_inertia * (r2x * py - r2y * px);
}

static void prestep(PG_World *w, float inv_dt)
{
    /* pass 1: masses, bias and approach speed. Must happen BEFORE any warm starting, otherwise the
     * warm-start impulses of one contact look like a fast impact to its neighbours. */
    for (int k = 0; k < w->ncur; k++) {
        Arbiter *ar = &w->cur[k];
        PG_Body *a = &w->bodies[ar->ia], *b = &w->bodies[ar->ib];
        float best = 0, bx_ = 0, by_ = 0;
        for (int i = 0; i < ar->n; i++) {
            Contact *c = &ar->c[i];
            c->r1x = c->px - a->x; c->r1y = c->py - a->y; c->r2x = c->px - b->x; c->r2y = c->py - b->y;
            float rn1 = c->r1x * c->nx + c->r1y * c->ny, rn2 = c->r2x * c->nx + c->r2y * c->ny;
            float kn = a->inv_mass + b->inv_mass + a->inv_inertia * (c->r1x * c->r1x + c->r1y * c->r1y - rn1 * rn1)
                     + b->inv_inertia * (c->r2x * c->r2x + c->r2y * c->r2y - rn2 * rn2);
            c->mass_n = kn > 0 ? 1.0f / kn : 0;
            float tx = c->ny, ty = -c->nx;
            float rt1 = c->r1x * tx + c->r1y * ty, rt2 = c->r2x * tx + c->r2y * ty;
            float kt = a->inv_mass + b->inv_mass + a->inv_inertia * (c->r1x * c->r1x + c->r1y * c->r1y - rt1 * rt1)
                     + b->inv_inertia * (c->r2x * c->r2x + c->r2y * c->r2y - rt2 * rt2);
            c->mass_t = kt > 0 ? 1.0f / kt : 0;
            float pos_bias = -BIAS_FACTOR * inv_dt * fminf(0.0f, c->sep + SLOP);
            if (pos_bias > MAX_BIAS) pos_bias = MAX_BIAS;
            float dvx = b->vx - b->w * c->r2y - a->vx + a->w * c->r1y;
            float dvy = b->vy + b->w * c->r2x - a->vy - a->w * c->r1x;
            float vn = dvx * c->nx + dvy * c->ny;
            float bounce = (vn < -BOUNCE_MIN) ? -ar->restitution * vn : 0.0f;
            c->bias = fmaxf(pos_bias, bounce);
            if (bounce > 0) { c->pn = 0; c->pt = 0; }
            if (-vn > best) { best = -vn; bx_ = c->px; by_ = c->py; }
        }
        if (ar->fresh && best > EVENT_MIN && w->nevents < MAX_EVENTS) w->events[w->nevents++] = (Event){ a, b, best, bx_, by_ };
    }
    /* pass 2: warm start with last step's accumulated impulses */
    for (int k = 0; k < w->ncur; k++) {
        Arbiter *ar = &w->cur[k];
        PG_Body *a = &w->bodies[ar->ia], *b = &w->bodies[ar->ib];
        for (int i = 0; i < ar->n; i++) {
            Contact *c = &ar->c[i];
            float tx = c->ny, ty = -c->nx;
            apply_imp(a, b, c->pn * c->nx + c->pt * tx, c->pn * c->ny + c->pt * ty, c->r1x, c->r1y, c->r2x, c->r2y);
        }
    }
}

static void solve_contact(const Arbiter *ar, Contact *c, PG_Body *a, PG_Body *b)
{
    float dvx = b->vx - b->w * c->r2y - a->vx + a->w * c->r1y;
    float dvy = b->vy + b->w * c->r2x - a->vy - a->w * c->r1x;
    float vn = dvx * c->nx + dvy * c->ny;
    float dPn = c->mass_n * (-vn + c->bias);
    float pn0 = c->pn;
    c->pn = fmaxf(pn0 + dPn, 0.0f);
    dPn = c->pn - pn0;
    apply_imp(a, b, dPn * c->nx, dPn * c->ny, c->r1x, c->r1y, c->r2x, c->r2y);

    dvx = b->vx - b->w * c->r2y - a->vx + a->w * c->r1y;
    dvy = b->vy + b->w * c->r2x - a->vy - a->w * c->r1x;
    float tx = c->ny, ty = -c->nx;
    float vt = dvx * tx + dvy * ty;
    float dPt = c->mass_t * (-vt);
    float max_pt = ar->friction * c->pn, pt0 = c->pt;
    c->pt = clampf(pt0 + dPt, -max_pt, max_pt);
    dPt = c->pt - pt0;
    apply_imp(a, b, dPt * tx, dPt * ty, c->r1x, c->r1y, c->r2x, c->r2y);
}

/* Fixed bottom-up order on purpose: with warm starting it converges faster for stacks than alternating sweeps. */
static void iterate(PG_World *w)
{
    for (int k = 0; k < w->ncur; k++) {
        Arbiter *ar = &w->cur[k];
        PG_Body *a = &w->bodies[ar->ia], *b = &w->bodies[ar->ib];
        for (int i = 0; i < ar->n; i++) solve_contact(ar, &ar->c[i], a, b);
    }
}

static void substep(PG_World *w, float dt)
{
    broadphase(w);
    for (int i = 0; i < w->high; i++) {
        PG_Body *b = &w->bodies[i];
        if (!b->alive || b->inv_mass == 0) continue;
        b->vx += (w->gx + b->fx * b->inv_mass) * dt;
        b->vy += (w->gy + b->fy * b->inv_mass) * dt;
        b->w  += b->inv_inertia * b->torque * dt;
        float ld = 1.0f / (1.0f + dt * b->lin_damp), ad = 1.0f / (1.0f + dt * b->ang_damp);
        b->vx *= ld; b->vy *= ld; b->w *= ad;
    }
    prestep(w, 1.0f / dt);
    for (int it = 0; it < w->iterations; it++) iterate(w);
    for (int i = 0; i < w->high; i++) {
        PG_Body *b = &w->bodies[i];
        if (!b->alive) continue;
        float sp = sqrtf(b->vx * b->vx + b->vy * b->vy);
        if (sp > MAX_SPEED) { float k = MAX_SPEED / sp; b->vx *= k; b->vy *= k; }
        b->w = clampf(b->w, -MAX_SPIN, MAX_SPIN);
        b->x += b->vx * dt; b->y += b->vy * dt;
        if (b->kind == PG_BODY_BOX) { b->angle += b->w * dt; b->c = cosf(b->angle); b->s = sinf(b->angle); }
        else if (b->kind == PG_BODY_CIRCLE) b->angle += b->w * dt;
    }
    Arbiter *t = w->prev; w->prev = w->cur; w->cur = t; w->nprev = w->ncur;
}

void pg_world_step(PG_World *w, float dt)
{
    w->nevents = 0;
    float h = dt / (float)w->substeps;
    for (int s = 0; s < w->substeps; s++) substep(w, h);
    for (int i = 0; i < w->high; i++) { w->bodies[i].fx = w->bodies[i].fy = w->bodies[i].torque = 0; }
    if (w->fn)
        for (int i = 0; i < w->nevents; i++) {
            Event *e = &w->events[i];
            if (e->a->alive && e->b->alive) w->fn(e->a, e->b, e->speed, e->x, e->y, w->ud);
        }
}

/* --------------------------------------------------------------- helpers */
void pg_body_local_to_world(const PG_Body *b, float lx, float ly, float *wx, float *wy)
{
    float c = b->kind == PG_BODY_CIRCLE ? cosf(b->angle) : b->c, s = b->kind == PG_BODY_CIRCLE ? sinf(b->angle) : b->s;
    *wx = b->x + c * lx - s * ly; *wy = b->y + s * lx + c * ly;
}

void pg_body_world_to_local(const PG_Body *b, float wx, float wy, float *lx, float *ly)
{
    float c = b->kind == PG_BODY_CIRCLE ? cosf(b->angle) : b->c, s = b->kind == PG_BODY_CIRCLE ? sinf(b->angle) : b->s;
    float dx = wx - b->x, dy = wy - b->y;
    *lx = c * dx + s * dy; *ly = -s * dx + c * dy;
}

bool pg_body_contains(const PG_Body *b, float x, float y)
{
    if (b->kind == PG_BODY_CIRCLE) { float dx = x - b->x, dy = y - b->y; return dx * dx + dy * dy <= b->radius * b->radius; }
    if (b->kind == PG_BODY_BOX) { float lx, ly; pg_body_world_to_local(b, x, y, &lx, &ly); return fabsf(lx) <= b->hx && fabsf(ly) <= b->hy; }
    return false;
}

PG_Body *pg_world_pick(PG_World *w, float x, float y)
{
    for (int i = w->high - 1; i >= 0; i--) {
        PG_Body *b = &w->bodies[i];
        if (b->alive && b->inv_mass > 0 && pg_body_contains(b, x, y)) return b;
    }
    return NULL;
}

void pg_body_apply_impulse(PG_Body *b, float ix, float iy, float wx, float wy)
{
    b->vx += ix * b->inv_mass; b->vy += iy * b->inv_mass;
    b->w += b->inv_inertia * ((wx - b->x) * iy - (wy - b->y) * ix);
}

void pg_body_apply_force(PG_Body *b, float fx, float fy) { b->fx += fx; b->fy += fy; }
float pg_body_speed(const PG_Body *b) { return sqrtf(b->vx * b->vx + b->vy * b->vy); }

void pg_body_grab_step(PG_Body *b, float lx, float ly, float tx, float ty, float freq, float zeta, float dt)
{
    if (b->inv_mass <= 0) return;
    float wx, wy;
    pg_body_local_to_world(b, lx, ly, &wx, &wy);
    float rx = wx - b->x, ry = wy - b->y;
    float omega = 2.0f * PI_F * freq;
    /* velocity of the grab point and its error to the finger */
    float cdx = b->vx - b->w * ry, cdy = b->vy + b->w * rx;
    float cx = wx - tx, cy = wy - ty;
    /* desired acceleration of that point: a damped spring (acceleration, so every mass feels the same stiffness) */
    float ax = -(omega * omega * cx + 2.0f * zeta * omega * cdx);
    float ay = -(omega * omega * cy + 2.0f * zeta * omega * cdy);
    float al = sqrtf(ax * ax + ay * ay);
    if (al > 30000.0f) { ax *= 30000.0f / al; ay *= 30000.0f / al; }
    /* impulse that produces it at the anchor: solve K j = a*dt */
    float k11 = b->inv_mass + b->inv_inertia * ry * ry;
    float k12 = -b->inv_inertia * rx * ry;
    float k22 = b->inv_mass + b->inv_inertia * rx * rx;
    float det = k11 * k22 - k12 * k12;
    if (fabsf(det) < 1e-18f) return;
    float rhsx = ax * dt, rhsy = ay * dt;
    float ix = (k22 * rhsx - k12 * rhsy) / det, iy = (k11 * rhsy - k12 * rhsx) / det;
    b->vx += b->inv_mass * ix; b->vy += b->inv_mass * iy;
    b->w += b->inv_inertia * (rx * iy - ry * ix);
}
