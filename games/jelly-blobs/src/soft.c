/* soft.c - soft-body jelly physics. See soft.h for the overview.
 *
 * Position based dynamics: predict, project constraints (shape matching, area, edge limits, collisions,
 * room bounds), derive velocities, damp.  Everything is clamped so a handful of fingers yanking the blobs
 * around cannot make anything explode.  No allocation, no globals. */
#include "soft.h"

#include <math.h>
#include <string.h>

#define PI_F        3.14159265358979f
#define TAU_F       6.28318530717959f

#define DT_TICK     (1.0f / 60.0f)
#define SUBSTEPS    2
#define CITER       2                 /* constraint iterations per substep */
#define GRAV        1000.0f
#define VMAX        1450.0f           /* particle speed cap, px/s */

#ifndef ALPHA_K
#define ALPHA_K 0.026f             /* shape stiffness per iteration at radius 72 */
#endif
#ifndef ROT_K
#define ROT_K 0.38f             /* fraction of the shape rotation that the rest shape follows */
#endif
#ifndef ROT_MAX
#define ROT_MAX 0.55f
#endif
#ifndef K_AREA
#define K_AREA 0.55f
#endif
#ifndef EDGE_MAX
#define EDGE_MAX 1.50f
#endif
#ifndef EDGE_MIN
#define EDGE_MIN 0.50f
#endif
#ifndef DAMP_INT
#define DAMP_INT 0.8f              /* internal (wobble) damping, 1/s */
#endif
#ifndef DAMP_AIR
#define DAMP_AIR 0.08f             /* whole-body drag, 1/s */
#endif
#ifndef MU_FLOOR
#define MU_FLOOR 0.085f            /* per substep tangential speed loss for particles on the floor */
#endif
#ifndef MU_WALL
#define MU_WALL 0.015f
#endif
#ifndef E_FLOOR
#define E_FLOOR 0.30f
#endif
#ifndef E_WALL
#define E_WALL 0.30f
#endif
#ifndef BOUNCE_K
#define BOUNCE_K 0.55f             /* extra rebound given at the bottom of a squash, as a fraction of the impact speed */
#endif
#ifndef HELD_SOFT
#define HELD_SOFT 0.16f            /* shape stiffness multiplier while a finger holds the blob: jelly in the hand is softer ... */
#endif
#ifndef HELD_GRAV
#define HELD_GRAV 2.6f             /* ... and heavier, so it droops from the fingertip */
#endif
#ifndef BEND_K
#define BEND_K 0.35f               /* skin smoothing per iteration where the skin is sharper than at rest: no spikes, no folds */
#endif
#ifndef REST_THR
#define REST_THR 90.0f             /* only bounce particles that hit faster than this, px/s */
#endif

#ifndef GRAB_K
#define GRAB_K 900.0f
#endif
#ifndef GRAB_C
#define GRAB_C 36.0f
#endif
#ifndef GRAB_LMAX
#define GRAB_LMAX 260.0f            /* the finger can only stretch the skin this far from its anchor */
#endif
#ifndef GRAB_AMAX
#define GRAB_AMAX 130000.0f
#endif

#ifndef COL_CIRCLE
#define COL_CIRCLE 0.82f             /* centre repulsion distance as a fraction of the summed radii */
#endif
#ifndef COL_K
#define COL_K 0.30f
#endif
#ifndef COL_PEN
#define COL_PEN 0.85f
#endif
#ifndef COL_MAXPEN
#define COL_MAXPEN 40.0f
#endif

enum { CF_FLOOR = 1, CF_WALL = 2, CF_CEIL = 4 };

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float sqr(float v) { return v * v; }

/* ------------------------------------------------------------------------- rest shape */
static void blob_make_rest(SoftBlob *b, float R)
{
    float rx[SB_N], ry[SB_N];
    for (int i = 0; i < SB_N; i++) {
        float a = -PI_F * 0.5f + TAU_F * (float)i / (float)SB_N;
        float x = cosf(a) * 1.07f, y = sinf(a);
        y = y < 0 ? y * 0.90f : y * 0.99f;             /* a mochi: wider than tall ... */
        if (y < -0.80f) y = -0.80f;                    /* ... with a flat belly on the floor */
        rx[i] = x; ry[i] = y;
    }
    float mx = 0, my = 0;
    for (int i = 0; i < SB_N; i++) { mx += rx[i]; my += ry[i]; }
    mx /= SB_N; my /= SB_N;
    float area = 0;
    for (int i = 0; i < SB_N; i++) {
        rx[i] -= mx; ry[i] -= my;
    }
    for (int i = 0; i < SB_N; i++) { int j = (i + 1) % SB_N; area += rx[i] * ry[j] - rx[j] * ry[i]; }
    area *= 0.5f;
    float s = R / sqrtf(area / PI_F);
    float lo = 1e9f, hi = -1e9f, left = 1e9f, right = -1e9f;
    for (int i = 0; i < SB_N; i++) {
        b->qx[i] = rx[i] * s; b->qy[i] = ry[i] * s;
        if (b->qy[i] < lo) lo = b->qy[i];
        if (b->qy[i] > hi) hi = b->qy[i];
        if (b->qx[i] < left) left = b->qx[i];
        if (b->qx[i] > right) right = b->qx[i];
    }
    b->qx[SB_C] = 0; b->qy[SB_C] = 0;
    for (int i = 0; i < SB_N; i++) {
        int j = (i + 1) % SB_N, h = (i + SB_N - 1) % SB_N;
        b->edge0[i] = sqrtf(sqr(b->qx[j] - b->qx[i]) + sqr(b->qy[j] - b->qy[i]));
        b->bend0[i] = sqrtf(sqr(0.5f * (b->qx[h] + b->qx[j]) - b->qx[i]) + sqr(0.5f * (b->qy[h] + b->qy[j]) - b->qy[i]));
    }
    b->radius = R;
    b->area0 = area * s * s;
    b->foot = -lo;
    b->wrest = right - left; b->hrest = hi - lo;
}

static void blob_place(SoftBlob *b, float cx, float cy)
{
    for (int i = 0; i < SB_NP; i++) {
        b->x[i] = cx + b->qx[i]; b->y[i] = cy + b->qy[i];
        b->vx[i] = 0; b->vy[i] = 0;
        b->ox[i] = b->x[i]; b->oy[i] = b->y[i];
        b->cf[i] = 0;
    }
}

void soft_init(SoftWorld *w, int n, const float *radius, const float *x, const float *y)
{
    memset(w, 0, sizeof *w);
    w->n = n > SB_MAX ? SB_MAX : n;
    for (int i = 0; i < w->n; i++) {
        SoftBlob *b = &w->b[i];
        blob_make_rest(b, radius[i]);
        b->alpha = clampf(ALPHA_K * 72.0f / radius[i], 0.014f, 0.08f);
        b->stretch_k = 1.0f;
        b->sx = b->sy = 1.0f;
        blob_place(b, x[i], y[i]);
        b->cx = x[i]; b->cy = y[i];
        b->ravg = b->rmax = radius[i];
        b->area = b->area0;
        b->air_ticks = 99;
    }
}

/* ---------------------------------------------------------------------- geometry helpers */
static void refresh_geometry(SoftBlob *b)
{
    float cx = 0, cy = 0;
    for (int i = 0; i < SB_N; i++) { cx += b->x[i]; cy += b->y[i]; }
    cx *= 1.0f / SB_N; cy *= 1.0f / SB_N;
    float sum = 0, mx = 0;
    for (int i = 0; i < SB_N; i++) {
        float d = sqrtf(sqr(b->x[i] - cx) + sqr(b->y[i] - cy));
        sum += d; if (d > mx) mx = d;
    }
    b->cx = cx; b->cy = cy; b->ravg = sum * (1.0f / SB_N); b->rmax = mx;
}

/* Find the wedge (ring edge k, k+1) that the ray from the centroid along (dx,dy) passes through.
 * t = (distance to the skin along that ray) / |(dx,dy)|, so the point is inside when t > 1. */
static bool sector_find(const SoftBlob *b, float dx, float dy, int *kout, float *tout)
{
    for (int k = 0; k < SB_N; k++) {
        int k1 = k + 1 == SB_N ? 0 : k + 1;
        float ax = b->x[k] - b->cx, ay = b->y[k] - b->cy, bx = b->x[k1] - b->cx, by = b->y[k1] - b->cy;
        float c1 = ax * dy - ay * dx;
        float c2 = dx * by - dy * bx;
        float cab = ax * by - ay * bx;
        if (c1 >= 0 && c2 >= 0 && cab > 1e-3f) {
            *kout = k;
            *tout = cab / (c1 + c2 + 1e-6f);
            return true;
        }
    }
    return false;
}

float soft_local_radius(const SoftBlob *b, float dx, float dy)
{
    float qd = sqrtf(dx * dx + dy * dy);
    if (qd < 1e-3f) return b->ravg;
    int k; float t;
    if (sector_find(b, dx, dy, &k, &t)) return qd * t;
    return b->ravg;
}

/* ------------------------------------------------------------------------ constraints */
static void shape_match(SoftBlob *b, float *theta_out)
{
    float cx = 0, cy = 0;
    for (int i = 0; i < SB_NP; i++) { cx += b->x[i]; cy += b->y[i]; }
    cx *= 1.0f / SB_NP; cy *= 1.0f / SB_NP;
    const float sx = b->sx, sy = b->sy;
    float sxx = 0, sxy = 0;
    for (int i = 0; i < SB_N; i++) {
        float px = b->x[i] - cx, py = b->y[i] - cy;
        float qx = b->qx[i] * sx, qy = b->qy[i] * sy;
        sxx += qx * px + qy * py;
        sxy += qx * py - qy * px;
    }
    float th = (sxx * sxx + sxy * sxy) > 1e-6f ? atan2f(sxy, sxx) : 0.0f;
    *theta_out = th;
    th = clampf(th * ROT_K, -ROT_MAX, ROT_MAX);
    float c = cosf(th), s = sinf(th), a = b->alpha * (b->held > 0 ? HELD_SOFT : 1.0f);
    for (int i = 0; i < SB_NP; i++) {
        float qx = b->qx[i] * sx, qy = b->qy[i] * sy;
        float gx = cx + c * qx - s * qy, gy = cy + s * qx + c * qy;
        b->x[i] += a * (gx - b->x[i]);
        b->y[i] += a * (gy - b->y[i]);
    }
}

static void area_constraint(SoftBlob *b, float target)
{
    float A = 0, gsum = 0;
    float gx[SB_N], gy[SB_N];
    for (int i = 0; i < SB_N; i++) {
        int j = i + 1 == SB_N ? 0 : i + 1, h = i == 0 ? SB_N - 1 : i - 1;
        A += b->x[i] * b->y[j] - b->x[j] * b->y[i];
        gx[i] = 0.5f * (b->y[j] - b->y[h]);
        gy[i] = 0.5f * (b->x[h] - b->x[j]);
        gsum += gx[i] * gx[i] + gy[i] * gy[i];
    }
    A *= 0.5f;
    b->area = A;
    if (gsum < 1.0f) return;
    float lam = K_AREA * (target - A) / gsum;
    /* never move a particle more than a fraction of the radius in one go */
    float lim = 0.25f * b->radius, gm = sqrtf(gsum / SB_N) + 1e-3f;
    lam = clampf(lam, -lim / gm, lim / gm);
    for (int i = 0; i < SB_N; i++) { b->x[i] += lam * gx[i]; b->y[i] += lam * gy[i]; }
}

/* A little bending stiffness: where the skin is sharper than at rest (a spike, a fold) pull the particle toward the
 * midpoint of its neighbours.  The rest shape and gentle wobbles are untouched.  Keeps the centroid. */
static void skin_smooth(SoftBlob *b)
{
    float dx[SB_N], dy[SB_N];
    for (int i = 0; i < SB_N; i++) {
        int j = i + 1 == SB_N ? 0 : i + 1, h = i == 0 ? SB_N - 1 : i - 1;
        float ex = 0.5f * (b->x[h] + b->x[j]) - b->x[i], ey = 0.5f * (b->y[h] + b->y[j]) - b->y[i];
        float d = sqrtf(ex * ex + ey * ey), over = d - b->bend0[i];
        float k = over <= 0 ? 0.0f : BEND_K * clampf(over / (0.32f * b->edge0[i]), 0.0f, 1.0f);
        dx[i] = ex * k; dy[i] = ey * k;
    }
    for (int i = 0; i < SB_N; i++) { b->x[i] += dx[i]; b->y[i] += dy[i]; }
}

static void edge_limits(SoftBlob *b)
{
    for (int i = 0; i < SB_N; i++) {
        int j = i + 1 == SB_N ? 0 : i + 1;
        float dx = b->x[j] - b->x[i], dy = b->y[j] - b->y[i];
        float l = sqrtf(dx * dx + dy * dy) + 1e-5f;
        float lo = b->edge0[i] * EDGE_MIN, hi = b->edge0[i] * EDGE_MAX, corr = 0;
        if (l > hi) corr = (l - hi) / l * 0.5f;
        else if (l < lo) corr = (l - lo) / l * 0.5f;
        if (corr != 0) {
            b->x[i] += dx * corr; b->y[i] += dy * corr;
            b->x[j] -= dx * corr; b->y[j] -= dy * corr;
        }
    }
}

static void room_bounds(SoftBlob *b, SoftWorld *w, int bi)
{
    for (int i = 0; i < SB_NP; i++) {
        unsigned char f = 0;
        if (b->y[i] < SB_FLOOR_Y) { b->y[i] = SB_FLOOR_Y; f |= CF_FLOOR; }
        else if (b->y[i] > SB_CEIL_Y) { b->y[i] = SB_CEIL_Y; f |= CF_CEIL; }
        if (b->x[i] < -SB_WALL_X) { b->x[i] = -SB_WALL_X; f |= CF_WALL; }
        else if (b->x[i] > SB_WALL_X) { b->x[i] = SB_WALL_X; f |= CF_WALL; }
        if (f) {
            b->cf[i] |= f;
            if ((f & CF_WALL) && b->wall_cool == 0 && fabsf(b->vpx[i]) > 240.0f && w->nev < 48) {
                SoftEvent *e = &w->ev[w->nev++];
                e->kind = SE_WALL; e->a = bi; e->b = -1; e->speed = fabsf(b->vpx[i]); e->x = b->x[i]; e->y = b->y[i];
                b->wall_cool = 14;
            }
        }
    }
}

/* ---------------------------------------------------------------------- blob vs blob */
static void collide_points(SoftBlob *p, SoftBlob *q, int pi, int qi, SoftWorld *w)
{
    float qr2 = q->rmax * q->rmax;
    for (int i = 0; i < SB_N; i++) {
        float dx = p->x[i] - q->cx, dy = p->y[i] - q->cy;
        if (dx * dx + dy * dy > qr2) continue;
        int k; float t;
        if (!sector_find(q, dx, dy, &k, &t) || t <= 1.0f) continue;
        int k1 = k + 1 == SB_N ? 0 : k + 1;
        float ex = q->x[k1] - q->x[k], ey = q->y[k1] - q->y[k];
        float el = sqrtf(ex * ex + ey * ey) + 1e-4f;
        float nx = ey / el, ny = -ex / el;
        float pen = -((p->x[i] - q->x[k]) * nx + (p->y[i] - q->y[k]) * ny);
        if (pen <= 0) continue;
        if (pen > COL_MAXPEN) pen = COL_MAXPEN;
        float push = pen * 0.5f * COL_PEN;
        p->x[i] += nx * push; p->y[i] += ny * push;
        q->x[k] -= nx * push; q->y[k] -= ny * push;
        q->x[k1] -= nx * push; q->y[k1] -= ny * push;
        int lo = pi < qi ? pi : qi, hi = pi < qi ? qi : pi;
        w->pair_touch[lo][hi] = 2;
    }
}

static void blob_collisions(SoftWorld *w)
{
    for (int a = 0; a < w->n; a++) refresh_geometry(&w->b[a]);
    for (int a = 0; a < w->n; a++)
        for (int c = a + 1; c < w->n; c++) {
            SoftBlob *A = &w->b[a], *B = &w->b[c];
            float dx = B->cx - A->cx, dy = B->cy - A->cy;
            float d2 = dx * dx + dy * dy, rr = A->rmax + B->rmax;
            if (d2 > rr * rr) continue;
            float d = sqrtf(d2);
            float mind = (A->ravg + B->ravg) * COL_CIRCLE;
            if (d < mind) {
                float nx = d > 1e-3f ? dx / d : 1.0f, ny = d > 1e-3f ? dy / d : 0.0f;
                float push = (mind - d) * 0.5f * COL_K;
                for (int i = 0; i < SB_NP; i++) {
                    A->x[i] -= nx * push; A->y[i] -= ny * push;
                    B->x[i] += nx * push; B->y[i] += ny * push;
                }
                w->pair_touch[a][c] = 2;
            }
            collide_points(A, B, a, c, w);
            collide_points(B, A, c, a, w);
        }
}

/* ------------------------------------------------------------------------- fingers */
static void apply_grabs(SoftWorld *w, float dt)
{
    for (int f = 0; f < SB_FINGERS; f++) {
        SoftGrab *g = &w->g[f];
        if (!g->active) continue;
        SoftBlob *b = &w->b[g->blob];
        for (int i = 0; i < SB_NP; i++) {
            float wi = g->w[i];
            if (wi <= 0.001f) continue;
            float dx = g->fx + g->ox[i] - b->x[i], dy = g->fy + g->oy[i] - b->y[i];
            float dl2 = dx * dx + dy * dy;
            if (dl2 > GRAB_LMAX * GRAB_LMAX) { float s = GRAB_LMAX / sqrtf(dl2); dx *= s; dy *= s; }
            float ax = wi * (GRAB_K * dx + GRAB_C * (g->fvx - b->vx[i]));
            float ay = wi * (GRAB_K * dy + GRAB_C * (g->fvy - b->vy[i]));
            float a2 = ax * ax + ay * ay;
            if (a2 > GRAB_AMAX * GRAB_AMAX) { float s = GRAB_AMAX / sqrtf(a2); ax *= s; ay *= s; }
            b->vx[i] += ax * dt; b->vy[i] += ay * dt;
        }
    }
}

int soft_pick(const SoftWorld *w, float x, float y, float slop)
{
    int best = -1; float bs = 1e9f;
    for (int i = 0; i < w->n; i++) {
        const SoftBlob *b = &w->b[i];
        float dx = x - b->cx, dy = y - b->cy;
        float d = sqrtf(dx * dx + dy * dy);
        float lr = soft_local_radius(b, dx, dy);
        float score = (d - lr) <= 0 ? d / (lr + 1e-3f) - 1.0f : (d - lr) / (slop + 1e-3f);   /* <0 inside, >0 outside */
        if ((d - lr) > slop) continue;
        if (score < bs) { bs = score; best = i; }
    }
    return best;
}

int soft_grab(SoftWorld *w, int finger, int blob, float x, float y)
{
    if (finger < 0 || finger >= SB_FINGERS) return -1;
    if (w->g[finger].active) soft_release(w, finger);
    if (blob < 0 || blob >= w->n) return -1;
    SoftGrab *g = &w->g[finger];
    SoftBlob *b = &w->b[blob];
    float sig2 = sqr(0.62f * b->radius);
    for (int i = 0; i < SB_NP; i++) {
        float dx = b->x[i] - x, dy = b->y[i] - y;
        g->w[i] = 0.05f + 0.95f * expf(-(dx * dx + dy * dy) / sig2);
        g->ox[i] = dx; g->oy[i] = dy;
    }
    g->active = true; g->blob = blob; g->age = 0;
    g->fx = x; g->fy = y; g->fvx = g->fvy = 0;
    b->held++;
    if (w->nev < 48) { SoftEvent *e = &w->ev[w->nev++]; e->kind = SE_GRAB; e->a = blob; e->b = finger; e->speed = 0; e->x = x; e->y = y; }
    return blob;
}

void soft_move(SoftWorld *w, int finger, float x, float y, float vx, float vy)
{
    if (finger < 0 || finger >= SB_FINGERS || !w->g[finger].active) return;
    SoftGrab *g = &w->g[finger];
    g->fx = clampf(x, -2000, 2000); g->fy = clampf(y, -2000, 2000);
    g->fvx = clampf(vx, -2200, 2200); g->fvy = clampf(vy, -2200, 2200);
    g->age++;
}

void soft_release(SoftWorld *w, int finger)
{
    if (finger < 0 || finger >= SB_FINGERS || !w->g[finger].active) return;
    SoftGrab *g = &w->g[finger];
    SoftBlob *b = &w->b[g->blob];
    g->active = false;
    if (b->held > 0) b->held--;
    float sp = sqrtf(g->fvx * g->fvx + g->fvy * g->fvy);
    if (sp > 250.0f) {                               /* a fling: hand a little of the finger's speed over */
        float k = 0.28f;
        float dvx = clampf((g->fvx - b->vcx) * k, -420, 420), dvy = clampf((g->fvy - b->vcy) * k, -420, 420);
        for (int i = 0; i < SB_NP; i++) { b->vx[i] += dvx; b->vy[i] += dvy; }
    }
    if (w->nev < 48) { SoftEvent *e = &w->ev[w->nev++]; e->kind = SE_RELEASE; e->a = g->blob; e->b = finger; e->speed = sp; e->x = b->cx; e->y = b->cy; }
}

bool soft_holding(const SoftWorld *w, int finger)
{
    return finger >= 0 && finger < SB_FINGERS && w->g[finger].active;
}

/* ---------------------------------------------------------------------------- pokes */
void soft_poke(SoftWorld *w, int bi, float x, float y, float speed)
{
    if (bi < 0 || bi >= w->n) return;
    SoftBlob *b = &w->b[bi];
    float dx = b->cx - x, dy = b->cy - y, d = sqrtf(dx * dx + dy * dy);
    if (d < 4.0f) { dx = 0; dy = -1; d = 1; }
    dx /= d; dy /= d;
    float sig2 = sqr(0.70f * b->radius);
    for (int i = 0; i < SB_NP; i++) {
        float ex = b->x[i] - x, ey = b->y[i] - y;
        float g = expf(-(ex * ex + ey * ey) / sig2);
        b->vx[i] += dx * speed * g; b->vy[i] += dy * speed * g;
    }
    b->ell_extra += (fabsf(dy) > fabsf(dx) ? -0.10f : 0.10f);     /* squash along the poke */
}

void soft_hop(SoftWorld *w, int bi, float up, float side)
{
    if (bi < 0 || bi >= w->n) return;
    SoftBlob *b = &w->b[bi];
    for (int i = 0; i < SB_NP; i++) { b->vy[i] += up; b->vx[i] += side; }
    b->ell_extra += 0.16f;
}

void soft_slap(SoftWorld *w, float x, float y, float range, float up)
{
    for (int i = 0; i < w->n; i++) {
        SoftBlob *b = &w->b[i];
        if (b->held > 0) continue;
        float dx = b->cx - x, dy = b->cy - y;
        float d = sqrtf(dx * dx + dy * dy) - b->ravg;
        if (d > range) continue;
        float k = clampf(1.0f - (d < 0 ? 0 : d) / range, 0.0f, 1.0f);
        if (b->lowest > 14.0f || b->vcy > 120.0f) continue;   /* only things standing on the mat feel it, and only once */
        float side = clampf(dx / (fabsf(dx) + 60.0f), -1, 1) * 70.0f * k;
        soft_hop(w, i, up * (0.45f + 0.55f * k), side);
    }
}

/* ------------------------------------------------------------------------ the step */
static void blob_heal(SoftWorld *w, int bi, bool nan_state)
{
    SoftBlob *b = &w->b[bi];
    float cx = b->cx, cy = b->cy;
    if (nan_state || !isfinite(cx) || !isfinite(cy)) { cx = 0; cy = SB_FLOOR_Y + b->foot + 120.0f; }
    cx = clampf(cx, -SB_WALL_X + b->radius, SB_WALL_X - b->radius);
    cy = clampf(cy, SB_FLOOR_Y + b->foot, SB_CEIL_Y - b->radius);
    blob_place(b, cx, cy);
    b->sx = b->sy = 1; b->ell = 0; b->ell_extra = 0; b->tilt = 0; b->bad_ticks = 0;
    b->cx = cx; b->cy = cy; b->vcx = b->vcy = 0; b->area = b->area0;
    if (w->nev < 48) { SoftEvent *e = &w->ev[w->nev++]; e->kind = SE_HEAL; e->a = bi; e->b = -1; e->speed = 0; e->x = cx; e->y = cy; }
}

static void substep(SoftWorld *w, float dt)
{
    /* forces */
    for (int i = 0; i < w->n; i++) {
        SoftBlob *b = &w->b[i];
        float drag = 1.0f - DAMP_AIR * dt, g = GRAV * (b->held > 0 ? HELD_GRAV : 1.0f);
        for (int p = 0; p < SB_NP; p++) { b->vy[p] -= g * dt; b->vx[p] *= drag; b->vy[p] *= drag; }
    }
    apply_grabs(w, dt);
    /* predict */
    for (int i = 0; i < w->n; i++) {
        SoftBlob *b = &w->b[i];
        for (int p = 0; p < SB_NP; p++) {
            float vx = b->vx[p], vy = b->vy[p];
            float sp2 = vx * vx + vy * vy;
            if (sp2 > VMAX * VMAX) { float s = VMAX / sqrtf(sp2); vx *= s; vy *= s; b->vx[p] = vx; b->vy[p] = vy; }
            b->vpx[p] = vx; b->vpy[p] = vy;
            b->ox[p] = b->x[p]; b->oy[p] = b->y[p];
            b->x[p] += vx * dt; b->y[p] += vy * dt;
            b->cf[p] = 0;
        }
    }
    /* constraints */
    for (int it = 0; it < CITER; it++) {
        for (int i = 0; i < w->n; i++) {
            SoftBlob *b = &w->b[i];
            float th;
            shape_match(b, &th);
            b->theta_raw = th;
            area_constraint(b, b->area0 * (1.0f + b->breath));
            skin_smooth(b);
            edge_limits(b);
        }
        blob_collisions(w);
        for (int i = 0; i < w->n; i++) room_bounds(&w->b[i], w, i);
    }
    /* velocities */
    for (int i = 0; i < w->n; i++) {
        SoftBlob *b = &w->b[i];
        float inv = 1.0f / dt, vcx = 0, vcy = 0;
        for (int p = 0; p < SB_NP; p++) {
            float vx = (b->x[p] - b->ox[p]) * inv, vy = (b->y[p] - b->oy[p]) * inv;
            unsigned char f = b->cf[p];
            if (f & CF_FLOOR) {
                if (b->vpy[p] < -REST_THR) vy = fmaxf(vy, -E_FLOOR * b->vpy[p]);
                vx *= 1.0f - MU_FLOOR;
            }
            if (f & CF_WALL) {
                if (fabsf(b->vpx[p]) > REST_THR) { float r = -E_WALL * b->vpx[p]; vx = b->vpx[p] > 0 ? fminf(vx, r) : fmaxf(vx, r); }
                vy *= 1.0f - MU_WALL;
            }
            if (f & CF_CEIL) { if (b->vpy[p] > REST_THR) vy = fminf(vy, -E_WALL * b->vpy[p]); }
            float sp2 = vx * vx + vy * vy;
            if (sp2 > VMAX * VMAX) { float s = VMAX / sqrtf(sp2); vx *= s; vy *= s; }
            b->vx[p] = vx; b->vy[p] = vy;
            vcx += vx; vcy += vy;
        }
        vcx *= 1.0f / SB_NP; vcy *= 1.0f / SB_NP;
        float k = expf(-DAMP_INT * dt);                 /* damp only the wobble, not the flight of the whole body */
        for (int p = 0; p < SB_NP; p++) {
            b->vx[p] = vcx + (b->vx[p] - vcx) * k;
            b->vy[p] = vcy + (b->vy[p] - vcy) * k;
        }
        for (int p = 0; p < SB_NP; p++) if (b->cf[p] & CF_FLOOR) b->floor_acc++;
        if (b->floor_acc > 255) b->floor_acc = 255;
    }
}

void soft_step(SoftWorld *w)
{
    for (int i = 0; i < w->n; i++) {
        SoftBlob *b = &w->b[i];
        b->floor_acc = 0;
        /* goal stretch from velocity (stretch when flying, squat when told to) and the game's extra pulse */
        float ell_v = clampf(0.00022f * fabsf(b->vcy), 0.0f, 0.20f) - clampf(0.00016f * fabsf(b->vcx), 0.0f, 0.14f);
        ell_v *= b->stretch_k * (b->held ? 0.35f : 1.0f);
        float ell_t = clampf(ell_v + b->ell_extra + b->ell_bias, -0.40f, 0.40f);
        b->ell += (ell_t - b->ell) * 0.30f;
        b->sy = expf(b->ell); b->sx = expf(-b->ell);
        b->ell_extra *= 0.92f;
        if (b->wall_cool > 0) b->wall_cool--;
    }
    for (int s = 0; s < SUBSTEPS; s++) substep(w, DT_TICK / (float)SUBSTEPS);

    /* contact events before the derived values are refreshed (they still hold last tick's velocities) */
    for (int a = 0; a < w->n; a++)
        for (int c = a + 1; c < w->n; c++) {
            if (w->bump_cool[a][c] > 0) w->bump_cool[a][c]--;
            int touching = w->pair_touch[a][c] > 0;
            if (touching && w->pair_touch[a][c] == 2) {          /* touched this tick */
                if (w->bump_cool[a][c] == 0 && w->nev < 48) {
                    const SoftBlob *A = &w->b[a], *B = &w->b[c];
                    float dx = B->cx - A->cx, dy = B->cy - A->cy, d = sqrtf(dx * dx + dy * dy) + 1e-3f;
                    float rel = (A->vcx - B->vcx) * dx / d + (A->vcy - B->vcy) * dy / d;
                    if (rel > 110.0f) {
                        SoftEvent *e = &w->ev[w->nev++];
                        e->kind = SE_BUMP; e->a = a; e->b = c; e->speed = rel;
                        e->x = (A->cx + B->cx) * 0.5f; e->y = (A->cy + B->cy) * 0.5f;
                        w->bump_cool[a][c] = 24;
                    }
                }
                w->pair_touch[a][c] = 1;
            } else w->pair_touch[a][c] = 0;
        }

    for (int i = 0; i < w->n; i++) {
        SoftBlob *b = &w->b[i];
        float vprev = b->vcy;
        float fin = 0;
        for (int p = 0; p < SB_NP; p++) fin += b->x[p] + b->y[p] + b->vx[p] + b->vy[p];
        bool nan_state = !isfinite(fin);
        if (nan_state) { blob_heal(w, i, true); continue; }

        refresh_geometry(b);
        float vsx = 0, vsy = 0;
        float mnx = 1e9f, mxx = -1e9f, mny = 1e9f, mxy = -1e9f;
        for (int p = 0; p < SB_NP; p++) { vsx += b->vx[p]; vsy += b->vy[p]; }
        b->vcx = vsx * (1.0f / SB_NP); b->vcy = vsy * (1.0f / SB_NP);
        for (int p = 0; p < SB_N; p++) {
            if (b->x[p] < mnx) mnx = b->x[p];
            if (b->x[p] > mxx) mxx = b->x[p];
            if (b->y[p] < mny) mny = b->y[p];
            if (b->y[p] > mxy) mxy = b->y[p];
        }
        b->minx = mnx; b->maxx = mxx; b->miny = mny; b->maxy = mxy;
        float area = 0;
        for (int p = 0; p < SB_N; p++) { int q = p + 1 == SB_N ? 0 : p + 1; area += b->x[p] * b->y[q] - b->x[q] * b->y[p]; }
        b->area = 0.5f * area;

        /* smoothed tilt for the face */
        float th = clampf(b->theta_raw * ROT_K, -ROT_MAX, ROT_MAX);
        b->tilt += (th - b->tilt) * 0.25f;
        /* how far from the rest shape */
        float w_ratio = (mxx - mnx) / b->wrest, h_ratio = (mxy - mny) / b->hrest;
        float dev = fmaxf(fmaxf(w_ratio, 1.0f / (w_ratio + 1e-3f)), fmaxf(h_ratio, 1.0f / (h_ratio + 1e-3f))) - 1.0f;
        b->stretch = clampf(dev * 2.0f, 0.0f, 1.0f);
        b->lowest = mny - SB_FLOOR_Y;

        /* grounded / landing bookkeeping */
        b->floor_n = b->floor_acc;
        if (b->floor_n >= 2) {
            if (b->air_ticks >= 5 && w->nev < 48) {
                float sp = -vprev;
                if (sp > 150.0f) {
                    SoftEvent *e = &w->ev[w->nev++];
                    e->kind = SE_LAND; e->a = i; e->b = -1; e->speed = sp; e->x = b->cx; e->y = b->miny;
                    b->land_boost = sp; b->land_age = 0;
                }
            }
            b->air_ticks = 0; b->ground_ticks++; b->grounded = true;
        } else {
            if (b->air_ticks < 100000) b->air_ticks++;
            b->ground_ticks = 0;
            b->grounded = b->air_ticks < 4 && b->lowest < 8.0f;
        }

        /* the rebound: when the squash reaches its bottom and the body starts to rise, give it a springy extra kick */
        if (b->land_boost > 0) {
            if (++b->land_age > 40 || b->held > 0) b->land_boost = 0;
            else if (b->vcy > 15.0f && b->floor_n > 0) {
                float dv = BOUNCE_K * b->land_boost;
                for (int p = 0; p < SB_NP; p++) b->vy[p] += dv;
                b->land_boost = 0;
            }
        }

        /* heal a blob that looks broken: inverted, collapsed, exploded */
        bool bad = b->area < 0.22f * b->area0 || b->area > 3.2f * b->area0 || b->rmax > 3.0f * b->radius ||
                   b->cx < -SB_WALL_X - 60 || b->cx > SB_WALL_X + 60 || b->cy < SB_FLOOR_Y - 60 || b->cy > SB_CEIL_Y + 60;
        if (bad) { if (++b->bad_ticks > 12) blob_heal(w, i, false); }
        else if (b->bad_ticks > 0) b->bad_ticks--;
    }

    for (int f = 0; f < SB_FINGERS; f++) if (w->g[f].active) w->g[f].age++;
    w->ticks++;
}

int soft_events(SoftWorld *w, const SoftEvent **out)
{
    int n = w->nev;
    *out = w->ev;
    w->nev = 0;
    return n;
}
