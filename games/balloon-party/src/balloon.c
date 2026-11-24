/* balloon.c - the balloons: mesh, buoyancy simulation, strings, picking, popping and batting.
 *
 * The simulation is my own small 3D one (<= 28 soft spheres, O(n^2) = ~400 pair tests per tick):
 *   - buoyancy: a constant lift that breathes slowly per balloon (so they drift up, hover, sink a little),
 *     linear air drag -> a gentle terminal speed; weak sideways sway; a travelling gust now and then;
 *   - soft sphere collisions (restitution 0.5, only for real impacts) -> they bump and jiggle;
 *   - the view box is the ceiling and the side walls; there is no floor: new balloons float up from below;
 *   - squash-and-stretch is a damped spring per balloon along an impact axis, applied in the model matrix;
 *   - strings are 6-point verlet chains drawn as thin quads in ONE dynamic mesh.
 * Everything is fixed-size, nothing allocates after init. */
#include "bp.h"

#include <stdlib.h>
#include <string.h>

#define SEGS    10
#define RINGS   7
#define KSEGS   5
#define KNOT_Y  1.42f            /* knot / string anchor, in balloon radii below the centre */
#define LIFT0   205.0f           /* px/s^2 */
#define DRAG    1.25f            /* 1/s */
#define GUST_A  120.0f
#define DT      PG_DT

typedef struct {
    bool     alive, entered, retiring;
    uint8_t  col;
    uint32_t gen;
    float x, y, z, vx, vy, vz;           /* world position (z = depth) and velocity */
    float r, z0;                         /* radius, preferred depth */
    float lift_w, lift_ph, float_h, home_x, sway_a, sway_w, sway_ph, bob_ph, curl_ph;
    float tilt, tiltv;                   /* roll about the view axis */
    float sq, sqv, sqa;                  /* squash amount (+stretch / -flatten), its velocity, axis angle */
    float bat_cd, bump_cd, age, grow, fade;
    PG_M4 M;
    float cx, cy;                        /* drawn centre (with the little bob) */
    float kx, ky;                        /* knot / string anchor */
    float sx[STR_N], sy[STR_N], px[STR_N], py[STR_N];
} Balloon;

static Balloon B[MAX_BALLOONS];
static PG_Mesh *m_balloon, *m_string;
static PG_Tex t_balloon, t_glint;
static uint32_t gen_ctr;
static float spawn_cd, spawn_phase, gust_next, gust_x, gust_dir;
static bool  gust_on, headless;
static int   cap = MAX_BALLOONS, cap_tick, last_col = -1;

/* --------------------------------------------------------------------- mesh */
/* Egg-shaped balloon profile at unit radius, theta 0 = top pole, pi = bottom: rho is the distance from the
 * axis, y the height. Slightly wider on top and tapering to the neck, 1.15 tall like the brief asks. */
static void profile(float th, float *rho, float *y, float *nr, float *ny)
{
    float s = sinf(th), c = cosf(th), sm = (1.0f - c) * 0.5f, K = 1.07f;
    *rho = s * (1.0f - 0.13f * sm) * K;
    *y = 1.15f * c;
    float drho = K * (c * (1.0f - 0.13f * sm) - 0.065f * s * s);
    float nx = 1.15f * s, nyv = drho, l = sqrtf(nx * nx + nyv * nyv);
    if (l < 1e-5f) { nx = 0; nyv = c >= 0 ? 1.0f : -1.0f; l = 1; }
    *nr = nx / l; *ny = nyv / l;
}

static void tri_out(PG_Mesh *m, int *ni, int a, int b, int c)
{
    const PG_Vertex *va = &m->v[a], *vb = &m->v[b], *vc = &m->v[c];
    PG_V3 e1 = pg_v3(vb->x - va->x, vb->y - va->y, vb->z - va->z), e2 = pg_v3(vc->x - va->x, vc->y - va->y, vc->z - va->z);
    PG_V3 g = pg_v3_cross(e1, e2);
    if (pg_v3_dot(g, g) < 1e-12f) return;                       /* degenerate (pole) */
    PG_V3 n = pg_v3(va->nx + vb->nx + vc->nx, va->ny + vb->ny + vc->ny, va->nz + vb->nz + vc->nz);
    if (pg_v3_dot(g, n) < 0) { int t = b; b = c; c = t; }       /* counter-clockwise seen from outside */
    m->idx[(*ni)++] = (uint16_t)a; m->idx[(*ni)++] = (uint16_t)b; m->idx[(*ni)++] = (uint16_t)c;
}

static void build_meshes(void)
{
    int nvb = (SEGS + 1) * (RINGS + 1), nvk = 1 + 2 * (KSEGS + 1);
    PG_Mesh *m = pg_mesh_new(nvb + nvk, SEGS * RINGS * 6 + KSEGS * 9);
    int nv = 0, ni = 0;
    for (int j = 0; j <= RINGS; j++) {
        float rho, y, nr, ny;
        profile(PG_PI * (float)j / (float)RINGS, &rho, &y, &nr, &ny);
        if (j == RINGS) y = -1.22f;                              /* a slightly pointed belly, the neck pulls the tip down */
        for (int i = 0; i <= SEGS; i++) {
            float ph = -PG_PI + PG_TAU * (float)i / (float)SEGS, sn = sinf(ph), cs = cosf(ph);
            PG_Vertex *v = &m->v[nv++];
            v->x = rho * sn; v->y = y; v->z = rho * cs;
            v->nx = nr * sn; v->ny = ny; v->nz = nr * cs;
            v->u = (float)i / (float)SEGS; v->v = (float)j / (float)RINGS;
            v->c = pg_rgb(255, 255, 255);
        }
    }
    for (int j = 0; j < RINGS; j++)
        for (int i = 0; i < SEGS; i++) {
            int a = j * (SEGS + 1) + i, b = a + 1, c = a + SEGS + 1, d = c + 1;
            tri_out(m, &ni, a, c, d);
            tri_out(m, &ni, a, d, b);
        }
    /* the little knot: a pinched neck flaring out */
    int kp = nv;
    PG_Vertex *v = &m->v[nv++];
    v->x = 0; v->y = -1.22f; v->z = 0; v->nx = 0; v->ny = -1; v->nz = 0; v->u = 0.5f; v->v = 1; v->c = pg_rgb(235, 235, 235);
    int k1 = nv;
    for (int i = 0; i <= KSEGS; i++) {
        float ph = PG_TAU * (float)i / (float)KSEGS, sn = sinf(ph), cs = cosf(ph);
        v = &m->v[nv++];
        v->x = 0.085f * sn; v->y = -1.29f; v->z = 0.085f * cs; v->nx = sn * 0.9f; v->ny = -0.3f; v->nz = cs * 0.9f;
        v->u = (float)i / (float)KSEGS; v->v = 1; v->c = pg_rgb(225, 225, 225);
    }
    int k2 = nv;
    for (int i = 0; i <= KSEGS; i++) {
        float ph = PG_TAU * (float)i / (float)KSEGS, sn = sinf(ph), cs = cosf(ph);
        v = &m->v[nv++];
        v->x = 0.15f * sn; v->y = -1.42f; v->z = 0.15f * cs; v->nx = sn * 0.9f; v->ny = -0.45f; v->nz = cs * 0.9f;
        v->u = (float)i / (float)KSEGS; v->v = 1; v->c = pg_rgb(215, 215, 215);
    }
    for (int i = 0; i < KSEGS; i++) {
        tri_out(m, &ni, kp, k1 + i, k1 + i + 1);
        tri_out(m, &ni, k1 + i, k2 + i, k2 + i + 1);
        tri_out(m, &ni, k1 + i, k2 + i + 1, k1 + i + 1);
    }
    m->nv = nv; m->ni = ni;
    m_balloon = m;

    /* all the strings share one dynamic mesh: 2 vertices per point, indices fixed per slot */
    m_string = pg_mesh_new(MAX_BALLOONS * STR_N * 2, MAX_BALLOONS * (STR_N - 1) * 6);
    int q = 0;
    for (int s = 0; s < MAX_BALLOONS; s++)
        for (int i = 0; i < STR_N - 1; i++) {
            int l0 = s * STR_N * 2 + i * 2, r0 = l0 + 1, l1 = l0 + 2, r1 = l0 + 3;
            m_string->idx[q++] = (uint16_t)l0; m_string->idx[q++] = (uint16_t)r0; m_string->idx[q++] = (uint16_t)r1;
            m_string->idx[q++] = (uint16_t)l0; m_string->idx[q++] = (uint16_t)r1; m_string->idx[q++] = (uint16_t)l1;
        }
    for (int i = 0; i < m_string->nv; i++) { m_string->v[i].nz = 1.0f; m_string->v[i].c = pg_rgb(255, 255, 255); m_string->v[i].u = 0.5f; m_string->v[i].v = 0.5f; }
}

void balloons_init(void)
{
    const char *drv = SDL_GetCurrentVideoDriver();
    headless = drv && !strcmp(drv, "dummy");
    t_balloon = tex_upload(paint_balloon(), true);
    t_glint = tex_upload(paint_glint(), false);
    build_meshes();
    memset(B, 0, sizeof B);
    gen_ctr = 0; spawn_cd = 1.0f; spawn_phase = 0.31f; gust_next = 2.4f; gust_on = false; cap = MAX_BALLOONS; cap_tick = 0; last_col = -1;
}

void balloons_free(void)
{
    pg_mesh_free(m_balloon); pg_mesh_free(m_string);
    m_balloon = m_string = NULL;
}

/* ------------------------------------------------------------------ helpers */
static inline float view_k(float z) { const Cam *c = cam_get(); return (c->dist - z) / c->focal; }

static int pick_colour(void)
{
    int cnt[N_COL] = { 0 }, lo = 1000;
    for (int i = 0; i < MAX_BALLOONS; i++) if (B[i].alive && !B[i].retiring) cnt[B[i].col]++;
    for (int k = 0; k < N_COL; k++) if (cnt[k] < lo) lo = cnt[k];
    int cand[N_COL], n = 0;
    for (int k = 0; k < N_COL; k++) if (cnt[k] <= lo && k != last_col) cand[n++] = k;
    if (n == 0) return pg_rand_int(0, N_COL - 1);
    return cand[pg_rand_int(0, n - 1)];
}

static void string_reset(Balloon *b)
{
    float seg = b->r * 0.30f;
    for (int i = 0; i < STR_N; i++) { b->sx[i] = b->px[i] = b->kx; b->sy[i] = b->py[i] = b->ky - seg * (float)i; }
}

static void compute_model(Balloon *b, float t)
{
    float a = clampf(1.0f + b->sq, 0.62f, 1.55f), bs = 1.0f / sqrtf(a), k = a - bs;
    float dx = cosf(b->sqa), dy = sinf(b->sqa), c = cosf(b->tilt), s = sinf(b->tilt);
    float u = b->r * b->grow * (b->retiring ? 0.25f + 0.75f * smooth01(b->fade) : 1.0f);
    float p = dx * c + dy * s, q = -dx * s + dy * c;
    float c0x = bs * c + k * dx * p, c0y = bs * s + k * dy * p;
    float c1x = -bs * s + k * dx * q, c1y = bs * c + k * dy * q;
    float bob = 3.4f * sinf(t * 1.9f + b->bob_ph);
    PG_M4 *M = &b->M;
    memset(M, 0, sizeof *M);
    M->m[0] = c0x * u; M->m[1] = c0y * u;
    M->m[4] = c1x * u; M->m[5] = c1y * u;
    M->m[10] = bs * u; M->m[15] = 1.0f;
    M->m[12] = b->x; M->m[13] = b->y + bob; M->m[14] = b->z;
    b->cx = b->x; b->cy = b->y + bob;
    b->kx = b->cx - c1x * u * KNOT_Y; b->ky = b->cy - c1y * u * KNOT_Y;
}

static void string_step(Balloon *b, float t)
{
    float seg = b->r * 0.30f;
    b->sx[0] = b->kx; b->sy[0] = b->ky;
    for (int i = 1; i < STR_N; i++) {
        float vx = (b->sx[i] - b->px[i]) * 0.955f, vy = (b->sy[i] - b->py[i]) * 0.955f;
        b->px[i] = b->sx[i]; b->py[i] = b->sy[i];
        float wob = sinf(t * 2.6f + b->curl_ph + (float)i * 1.3f) * 70.0f * (float)i / (float)STR_N;
        b->sx[i] += vx + wob * DT * DT;
        b->sy[i] += vy - 460.0f * DT * DT;
    }
    for (int it = 0; it < 2; it++)
        for (int i = 1; i < STR_N; i++) {
            float dx = b->sx[i] - b->sx[i - 1], dy = b->sy[i] - b->sy[i - 1], d = sqrtf(dx * dx + dy * dy);
            if (d < 1e-4f) { dx = 0; dy = -1; d = 1; }
            float k = seg / d;
            b->sx[i] = b->sx[i - 1] + dx * k; b->sy[i] = b->sy[i - 1] + dy * k;
        }
}

static void kick(Balloon *b, float nx, float ny, float amount)
{
    /* amount < 0 flattens along (nx,ny), > 0 stretches along it */
    float ang = atan2f(ny, nx);
    if (fabsf(amount) > 0.5f * fabsf(b->sqv) || fabsf(b->sq) < 0.04f) b->sqa = ang;
    b->sqv += amount;
    b->sqv = clampf(b->sqv, -8.0f, 8.0f);
}

/* ------------------------------------------------------------------- spawn */
static int spawn(bool below)
{
    int i = -1;
    for (int k = 0; k < MAX_BALLOONS; k++) if (!B[k].alive) { i = k; break; }
    if (i < 0) return -1;
    const Cam *c = cam_get();
    Balloon *b = &B[i];
    memset(b, 0, sizeof *b);
    b->alive = true; b->gen = ++gen_ctr;
    b->col = (uint8_t)pick_colour(); last_col = b->col;
    b->r = 62.0f - 2.45f * (float)b->col + frand(-2.5f, 2.5f);
    b->z = b->z0 = frand(-90.0f, 90.0f);
    float k = view_k(b->z);
    spawn_phase += 0.618034f; spawn_phase -= floorf(spawn_phase);
    b->x = c->ex + (spawn_phase * 2.0f - 1.0f) * 0.80f * 480.0f * k;
    if (below) { b->y = c->ey - 270.0f * k - b->r * 1.25f - frand(0, 24); b->vy = 280.0f; b->grow = 1.0f; }
    else { b->y = c->ey + frand(-0.25f, 0.80f) * 270.0f * k; b->vy = frand(0, 30); b->grow = 1.0f; b->entered = true; }
    b->vx = frand(-18, 18);
    /* every balloon has its own altitude it floats at: some press against the ceiling, the rest hover at
     * different heights (0 = bottom edge of the view, 1 = top), and that height wanders slowly */
    b->float_h = pg_randf() < 0.30f ? 1.25f : frand(0.12f, 0.86f);
    b->home_x = frand(-0.85f, 0.85f);                            /* and a lazy home column so the room stays balanced */
    b->lift_w = frand(0.25f, 0.6f);
    b->lift_ph = frand(0, PG_TAU);
    b->sway_a = frand(26.0f, 64.0f); b->sway_w = frand(0.45f, 1.05f); b->sway_ph = frand(0, PG_TAU);
    b->bob_ph = frand(0, PG_TAU); b->curl_ph = frand(0, PG_TAU);
    b->sqa = 0; b->fade = 1.0f;
    b->age = below ? 0.0f : frand(0, 8);
    compute_model(b, pg_time());
    string_reset(b);
    return i;
}

/* --------------------------------------------------------------- simulation */
static void collide(bool impulse)
{
    for (int i = 0; i < MAX_BALLOONS; i++) {
        Balloon *a = &B[i];
        if (!a->alive) continue;
        for (int j = i + 1; j < MAX_BALLOONS; j++) {
            Balloon *b = &B[j];
            if (!b->alive) continue;
            float dx = b->x - a->x, dy = b->y - a->y, dz = (b->z - a->z) * 0.8f;
            float R = (a->r + b->r) * 1.0f, d2 = dx * dx + dy * dy + dz * dz;
            if (d2 >= R * R) continue;
            float d = sqrtf(d2);
            if (d < 1e-3f) { dx = frand(-1, 1); dy = 1; dz = 0; d = sqrtf(dx * dx + dy * dy); }
            float nx = dx / d, ny = dy / d, nz = dz / d;
            float ma = a->r * a->r, mb = b->r * b->r, wa = 1.0f / ma, wb = 1.0f / mb, ws = wa + wb;
            float pen = R - d;
            float corr = 0.55f * pen;
            a->x -= nx * corr * wa / ws; a->y -= ny * corr * wa / ws; a->z -= nz * corr * wa / ws * 1.25f;
            b->x += nx * corr * wb / ws; b->y += ny * corr * wb / ws; b->z += nz * corr * wb / ws * 1.25f;
            if (!impulse) continue;
            float vn = (b->vx - a->vx) * nx + (b->vy - a->vy) * ny + (b->vz - a->vz) * nz;
            if (vn >= 0) continue;
            float e = vn < -45.0f ? 0.5f : 0.0f;
            float J = -(1.0f + e) * vn / ws;
            a->vx -= nx * J * wa; a->vy -= ny * J * wa; a->vz -= nz * J * wa;
            b->vx += nx * J * wb; b->vy += ny * J * wb; b->vz += nz * J * wb;
            if (-vn > 55.0f) {
                if (a->bump_cd <= 0) { kick(a, nx, ny, -clampf(-vn * 0.028f, 0.5f, 6.0f)); a->bump_cd = 0.12f; }
                if (b->bump_cd <= 0) { kick(b, nx, ny, -clampf(-vn * 0.028f, 0.5f, 6.0f)); b->bump_cd = 0.12f; }
                if (-vn > 210.0f) snd_bump(-vn);
            }
        }
    }
}

static int count_active(void)
{
    int n = 0;
    for (int i = 0; i < MAX_BALLOONS; i++) if (B[i].alive && !B[i].retiring) n++;
    return n;
}

int balloons_count(void) { return count_active(); }
int balloon_slots(void) { return MAX_BALLOONS; }

#ifdef BP_DEBUG
void balloons_debug_dump(void)
{
    int vis = 0;
    for (int i = 0; i < MAX_BALLOONS; i++) {
        float sx, sy, sr;
        if (B[i].alive && balloon_screen(i, &sx, &sy, &sr) && sy - sr < 540.0f) vis++;
    }
    pg_log("  visible=%d", vis);
    for (int i = 0; i < MAX_BALLOONS; i++) {
        float sx, sy, sr;
        if (getenv("BP_DUMP_ALL") && B[i].alive && balloon_screen(i, &sx, &sy, &sr))
            pg_log("  b%02d col=%d at (%.0f,%.0f) sr=%.0f z=%.0f v=(%.0f,%.0f) fh=%.2f%s", i, B[i].col, sx, sy, sr, B[i].z, B[i].vx, B[i].vy, B[i].float_h, B[i].retiring ? " retiring" : "");
    }
}
#endif

void balloons_update(float t)
{
    const Cam *c = cam_get();
    if (gen_ctr == 0) {                                         /* first tick: a party already in progress */
        for (int k = 0; k < 14; k++) spawn(false);
        for (int it = 0; it < 60; it++) collide(false);
        for (int i = 0; i < MAX_BALLOONS; i++) if (B[i].alive) { compute_model(&B[i], t); string_reset(&B[i]); }
    }

    /* a gust now and then: a soft wave of wind that crosses the room and lifts balloons a little */
    gust_next -= DT;
    if (!gust_on && gust_next <= 0) { gust_on = true; gust_dir = pg_rand() & 1 ? 1.0f : -1.0f; gust_x = gust_dir > 0 ? -120.0f : PG_W + 120.0f; }
    if (gust_on) {
        gust_x += gust_dir * 380.0f * DT;
        if (gust_x < -260.0f || gust_x > PG_W + 260.0f) { gust_on = false; gust_next = frand(3.5f, 6.5f); }
    }

    /* how many balloons the frame time allows (the Pi is slow; children pile things up forever) */
    if (++cap_tick >= 45) {
        cap_tick = 0;
        float ms = headless ? 0.0f : pg_frame_ms();
        if (ms > 12.0f && cap > 14) cap--;
        else if (ms < 8.5f && cap < MAX_BALLOONS) cap++;
        if (count_active() > cap) {
            int oldest = -1; float age = -1;
            for (int i = 0; i < MAX_BALLOONS; i++) if (B[i].alive && !B[i].retiring && B[i].age > age) { age = B[i].age; oldest = i; }
            if (oldest >= 0) { B[oldest].retiring = true; fx_burst(pg_v3(B[oldest].cx, B[oldest].cy, B[oldest].z + B[oldest].r * 0.5f), 45.0f * (float)B[oldest].col, 6, 110.0f); }
        }
    }

    int n = count_active();
    spawn_cd -= DT;
    if (n < MIN_BALLOONS && spawn_cd > 0.22f) spawn_cd = 0.22f;
    if (spawn_cd <= 0) {
        if (n < cap) spawn(true);
        spawn_cd = n < MIN_BALLOONS ? 0.22f : n < 16 ? frand(0.4f, 0.65f) : n < 22 ? frand(1.4f, 2.2f) : frand(2.6f, 3.6f);
    }

    for (int i = 0; i < MAX_BALLOONS; i++) {
        Balloon *b = &B[i];
        if (!b->alive) continue;
        b->age += DT;
        if (b->bat_cd > 0) b->bat_cd -= DT;
        if (b->bump_cd > 0) b->bump_cd -= DT;
        if (b->retiring) { b->fade -= DT / 0.8f; if (b->fade <= 0) { b->alive = false; continue; } }
        float k = view_k(b->z);
        float sxs = PG_W * 0.5f + (b->x - c->ex) / k;
        float yn = (b->y - (c->ey - 270.0f * k)) / (540.0f * k);
        float ht = b->float_h + (b->float_h > 1.0f ? 0.0f : 0.10f * sinf(b->lift_w * b->age + b->lift_ph));
        float lift = LIFT0 * clampf(3.2f * (ht - yn), -0.55f, 1.35f);
        float xn = (b->x - c->ex) / (480.0f * k);
        float hx = b->home_x + 0.25f * sinf(b->lift_w * 0.8f * b->age + b->sway_ph);
        float ax = b->sway_a * sinf(b->sway_w * t + b->sway_ph) - 62.0f * (xn - hx), ay = lift;
        if (gust_on) {
            float d = (sxs - gust_x) / 170.0f, w = expf(-d * d);
            ax += gust_dir * GUST_A * w; ay += GUST_A * 0.45f * w;
        }
        /* a soft floor just above the bottom edge turns sinking balloons around; it is armed only once a balloon has
         * risen above it, so balloons floating up from below the screen are not shoved */
        float ymin = c->ey - 270.0f * k + 34.0f * k + 1.15f * b->r;
        if (!b->entered && b->y > ymin) b->entered = true;
        if (b->entered && b->y < ymin) ay += 6.0f * (ymin - b->y);
        float az = -1.1f * (b->z - b->z0);
        b->vx += ax * DT; b->vy += ay * DT; b->vz += az * DT;
        float f = 1.0f / (1.0f + DRAG * DT);
        b->vx *= f; b->vy *= f; b->vz *= 1.0f / (1.0f + 2.4f * DT);
        b->x += b->vx * DT; b->y += b->vy * DT; b->z += b->vz * DT;
        b->z = clampf(b->z, -110.0f, 110.0f);
    }

    collide(true);
    collide(false);

    for (int i = 0; i < MAX_BALLOONS; i++) {
        Balloon *b = &B[i];
        if (!b->alive) continue;
        float k = view_k(b->z);
        float xmin = c->ex - 480.0f * k + 10.0f * k + b->r, xmax = c->ex + 480.0f * k - 10.0f * k - b->r;
        float ymax = c->ey + 270.0f * k - 14.0f * k - 1.17f * b->r;
        if (b->x < xmin) { b->x = xmin; if (b->vx < 0) { if (-b->vx > 40) kick(b, 1, 0, -clampf(-b->vx * 0.02f, 0.3f, 5.0f)); b->vx = b->vx < -30 ? -b->vx * 0.5f : 0; } }
        if (b->x > xmax) { b->x = xmax; if (b->vx > 0) { if (b->vx > 40) kick(b, 1, 0, -clampf(b->vx * 0.02f, 0.3f, 5.0f)); b->vx = b->vx > 30 ? -b->vx * 0.5f : 0; } }
        if (b->y > ymax) {
            b->y = ymax;
            if (b->vy > 0) {
                if (b->vy > 45 && b->bump_cd <= 0) { kick(b, 0, 1, -clampf(b->vy * 0.022f, 0.4f, 5.5f)); b->bump_cd = 0.12f; if (b->vy > 200) snd_bump(b->vy); }
                b->vy = b->vy > 40 ? -b->vy * 0.5f : 0;
            }
        }
        /* squash spring (jelly), roll spring (a pendulum leaning into the motion) */
        b->sqv += (-340.0f * b->sq - 2.0f * 0.2f * 18.4f * b->sqv) * DT;
        b->sq += b->sqv * DT;
        b->sq = clampf(b->sq, -0.42f, 0.5f);
        float tt = clampf(-b->vx * 0.0018f, -0.45f, 0.45f);
        b->tiltv += (-34.0f * (b->tilt - tt) - 3.4f * b->tiltv) * DT;
        b->tilt += b->tiltv * DT;
        compute_model(b, t);
        string_step(b, t);
    }
}

/* -------------------------------------------------------------------- drawing */
void balloons_draw_shadows(void)
{
    PG_Tex sh = pg3d_builtin(PG3D_TEX_SHADOW);
    for (int i = 0; i < MAX_BALLOONS; i++) {
        const Balloon *b = &B[i];
        if (!b->alive) continue;
        float dz = b->z - WALL_Z;
        float al = 0.26f * (b->retiring ? b->fade : 1.0f) * (1.0f - 0.30f * dz / 450.0f);
        pg3d_sprite(sh, pg_v3(b->cx + 0.17f * dz, b->cy - 0.25f * dz, WALL_Z + 8.0f), b->r * 2.3f, b->r * 2.8f, 0,
                    pg_rgba(255, 255, 255, (uint8_t)(clampf(al, 0, 1) * 255.0f)), PG3D_BG);
    }
}

void balloons_draw(void)
{
    const Cam *cm = cam_get();
    PG_V3 ld = pg_v3_norm(pg_v3(LIGHT_X, LIGHT_Y, LIGHT_Z));
    int ns = 0;
    for (int i = 0; i < MAX_BALLOONS; i++) {
        const Balloon *b = &B[i];
        if (!b->alive) continue;
        uint8_t al = (uint8_t)(clampf(b->retiring ? b->fade : 1.0f, 0, 1) * 255.0f);
        pg3d_mesh(m_balloon, t_balloon, &b->M, bal_color(b->col, al), 0);

        /* gloss: the SDK's soft shine plus a crisp window reflection, both riding on the lit side */
        float u = b->r * b->grow * (b->retiring ? 0.25f + 0.75f * smooth01(b->fade) : 1.0f);
        PG_V3 ctr = pg_v3(b->cx, b->cy, b->z);
        pg3d_shine(ctr, u, 0.55f * (float)al / 255.0f);
        PG_V3 toc = pg_v3_norm(pg_v3_sub(cm->pg.eye, ctr));
        PG_V3 h = pg_v3_norm(pg_v3_add(ld, toc));
        PG_V3 gp = pg_v3_add(ctr, pg_v3(h.x * u * 0.80f - u * 0.10f, h.y * u * 0.92f + u * 0.06f, h.z * u * 0.80f + u * 0.52f));
        pg3d_sprite(t_glint, gp, u * 0.78f, u * 0.78f, 0.0f, pg_rgba(255, 255, 255, (uint8_t)(0.92f * (float)al)), 0);

        /* string vertices */
        PG_Vertex *v = &m_string->v[ns * STR_N * 2];
        PG_Color sc = pg_color_lerp(bal_color(b->col, 255), pg_rgb(255, 255, 255), 0.5f);
        for (int p = 0; p < STR_N; p++) {
            int p0 = p > 0 ? p - 1 : 0, p1 = p < STR_N - 1 ? p + 1 : STR_N - 1;
            float dx = b->sx[p1] - b->sx[p0], dy = b->sy[p1] - b->sy[p0], d = sqrtf(dx * dx + dy * dy);
            if (d < 1e-4f) { dx = 0; dy = -1; d = 1; }
            float nx = -dy / d, ny = dx / d;
            float curl = 2.4f * sinf((float)p * 1.05f + b->curl_ph) * (float)p / (float)(STR_N - 1);
            float cx = b->sx[p] + nx * curl, cy = b->sy[p] + ny * curl;
            float w = lerpf(1.45f, 0.95f, (float)p / (float)(STR_N - 1)) * (0.7f + 0.3f * 600.0f / (cm->dist - b->z));
            PG_Color c = sc; c.a = (uint8_t)((float)al * lerpf(1.0f, 0.55f, (float)p / (float)(STR_N - 1)));
            PG_Vertex *l = &v[p * 2], *r = &v[p * 2 + 1];
            l->x = cx + nx * w; l->y = cy + ny * w; l->z = b->z;
            r->x = cx - nx * w; r->y = cy - ny * w; r->z = b->z;
            l->c = c; r->c = c;
        }
        ns++;
    }
    m_string->nv = ns * STR_N * 2;
    m_string->ni = ns * (STR_N - 1) * 6;
    if (ns > 0) {
        PG_M4 id = pg_m4_identity();
        pg3d_mesh(m_string, pg3d_white(), &id, pg_rgb(255, 255, 255), PG3D_UNLIT | PG3D_TWOSIDED | PG3D_NOFOG);
    }
}

/* ------------------------------------------------------------------ picking */
uint32_t balloon_gen(int i) { return (i >= 0 && i < MAX_BALLOONS) ? B[i].gen : 0; }
bool balloon_alive(int i, uint32_t gen) { return i >= 0 && i < MAX_BALLOONS && B[i].alive && B[i].gen == gen && !B[i].retiring; }

bool balloon_screen(int i, float *sx, float *sy, float *sr)
{
    if (i < 0 || i >= MAX_BALLOONS || !B[i].alive) return false;
    const Balloon *b = &B[i];
    if (!cam_project(b->cx, b->cy, b->z, sx, sy)) return false;
    *sr = b->r * b->grow * cam_scale(b->z);
    return true;
}

static float ellipse_q(const Balloon *b, float sx, float sy, float pad, float *ox, float *oy)
{
    float bx, by;
    if (!cam_project(b->cx, b->cy, b->z, &bx, &by)) return 1e9f;
    float s = cam_scale(b->z), rx = b->r * s + pad, ry = 1.15f * b->r * s + pad;
    float dx = (sx - bx) / rx, dy = (sy - by) / ry;
    if (ox) *ox = bx;
    if (oy) *oy = by;
    return dx * dx + dy * dy;
}

bool balloon_contains(int i, float sx, float sy, float pad)
{
    if (i < 0 || i >= MAX_BALLOONS || !B[i].alive || B[i].retiring) return false;
    return ellipse_q(&B[i], sx, sy, pad, NULL, NULL) <= 1.0f;
}

int balloon_hit(float sx, float sy, float pad)
{
    int best = -1; float bz = -1e9f, bq = 1e9f;
    for (int i = 0; i < MAX_BALLOONS; i++) {
        const Balloon *b = &B[i];
        if (!b->alive || b->retiring) continue;
        float q = ellipse_q(b, sx, sy, pad, NULL, NULL);
        if (q > 1.0f) continue;
        if (b->z > bz + 8.0f || (b->z > bz - 8.0f && q < bq)) { best = i; bz = b->z; bq = q; }
    }
    return best;
}

/* does the finger's path this tick pass through the balloon (swept test in the balloon's own ellipse space)? */
bool balloon_swept(int i, float x0, float y0, float x1, float y1, float pad)
{
    if (i < 0 || i >= MAX_BALLOONS || !B[i].alive || B[i].retiring) return false;
    const Balloon *b = &B[i];
    float bx, by;
    if (!cam_project(b->cx, b->cy, b->z, &bx, &by)) return false;
    float s = cam_scale(b->z), rx = b->r * s + pad, ry = 1.15f * b->r * s + pad;
    float ax = (x0 - bx) / rx, ay = (y0 - by) / ry, ex = (x1 - bx) / rx, ey = (y1 - by) / ry;
    float dx = ex - ax, dy = ey - ay, l2 = dx * dx + dy * dy;
    float t = l2 > 1e-9f ? clampf(-(ax * dx + ay * dy) / l2, 0, 1) : 0;
    float qx = ax + dx * t, qy = ay + dy * t;
    return qx * qx + qy * qy <= 1.0f;
}

/* a balloon worth pointing at: comfortably on screen, away from the top corners, big and near the middle.
 * `cur` is kept while it is still a good choice, so the hand does not hop around. */
static float hint_score(int i)
{
    const Balloon *b = &B[i];
    float sx, sy, sr;
    if (!b->alive || b->retiring || !b->entered || !balloon_screen(i, &sx, &sy, &sr)) return -1e9f;
    if (sy < 130 || sy > 420 || sx < 150 || sx > 810) return -1e8f;
    return sr * 0.5f - hypotf(sx - 480.0f, (sy - 280.0f) * 1.3f) * 0.45f + b->z * 0.1f;
}

int balloon_for_hint(int cur, uint32_t cur_gen)
{
    int best = -1; float bs = -1e18f;
    for (int i = 0; i < MAX_BALLOONS; i++) {
        float sc = hint_score(i);
        if (sc > bs) { bs = sc; best = i; }
    }
    if (cur >= 0 && balloon_alive(cur, cur_gen)) {
        float cs = hint_score(cur);
        if (cs > -1e7f && cs >= bs - 45.0f) return cur;
    }
    if (best < 0)
        for (int i = 0; i < MAX_BALLOONS; i++) if (B[i].alive && !B[i].retiring) { best = i; break; }
    return best;
}

/* ------------------------------------------------------------- interactions */
void balloon_pop(int i)
{
    if (i < 0 || i >= MAX_BALLOONS || !B[i].alive) return;
    Balloon *b = &B[i];
    PG_V3 p = pg_v3(b->cx, b->cy, b->z);
    float u = b->r * b->grow;
    fx_pop(p, b->col, u);
    snd_pop(b->col, b->r);
    /* the burst of air shoves close neighbours away and makes them wobble */
    for (int j = 0; j < MAX_BALLOONS; j++) {
        Balloon *o = &B[j];
        if (j == i || !o->alive) continue;
        float dx = o->x - b->x, dy = o->y - b->y, dz = (o->z - b->z) * 0.6f, d = sqrtf(dx * dx + dy * dy + dz * dz);
        float R = (b->r + o->r) * 2.3f;
        if (d >= R || d < 1e-3f) continue;
        float w = 1.0f - d / R, push = 150.0f * w * (52.0f / o->r);
        o->vx += dx / d * push; o->vy += dy / d * push; o->vz += dz / d * push * 0.5f;
        kick(o, dx, dy, -3.0f * w);
    }
#ifdef BP_DEBUG
    if (getenv("BP_PRIZES") || pg_randf() < 0.20f) fx_prize(p);     /* dev only: BP_PRIZES=1 releases a prize from every pop */
#else
    if (pg_randf() < 0.20f) fx_prize(p);
#endif
    b->alive = false;
    if (spawn_cd > 0.5f) spawn_cd = frand(0.3f, 0.7f);
}

void balloon_bat(int i, float svx, float svy)
{
    if (i < 0 || i >= MAX_BALLOONS || !B[i].alive || B[i].retiring) return;
    Balloon *b = &B[i];
    if (b->bat_cd > 0) return;
    float k = view_k(b->z);
    float wx = svx * k, wy = -svy * k, sp = sqrtf(wx * wx + wy * wy);
    if (sp < 1.0f) return;
    float dx = wx / sp, dy = wy / sp;
    float imp = clampf(sp * 0.62f, 170.0f, 800.0f) * clampf(54.0f / b->r, 0.8f, 1.25f);
    b->vx = b->vx * 0.25f + dx * imp;
    b->vy = b->vy * 0.25f + dy * imp;
    b->vz += frand(-70.0f, 70.0f);
    b->tiltv += -dx * 3.2f + frand(-2.0f, 2.0f);
    kick(b, dx, dy, 6.0f);
    b->bat_cd = 0.22f;
    snd_bat(b->r, clampf(imp / 800.0f, 0, 1));
    fx_burst(pg_v3(b->cx, b->cy, b->z + b->r * 0.5f), (float)b->col * 45.0f, 5, 150.0f);
}

void balloons_puff(float sx, float sy)
{
    for (int i = 0; i < MAX_BALLOONS; i++) {
        Balloon *b = &B[i];
        if (!b->alive || b->retiring) continue;
        float bx, by;
        if (!cam_project(b->cx, b->cy, b->z, &bx, &by)) continue;
        float dx = bx - sx, dy = by - sy, d = sqrtf(dx * dx + dy * dy), R = 190.0f;
        if (d >= R) continue;
        if (d < 1.0f) { dx = 0; dy = 1; d = 1; }
        float w = 1.0f - d / R, k = view_k(b->z), push = 170.0f * w * w * clampf(54.0f / b->r, 0.8f, 1.2f);
        b->vx += dx / d * push * k; b->vy += -dy / d * push * k;
        kick(b, dx, -dy, -2.0f * w);
    }
}
