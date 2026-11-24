/* fx.c - everything that makes a touch feel good: pop confetti, sparkles, ripples, 3D prizes (a spinning
 * star / heart / music note), the soft light under each finger and the pointing hint hand.
 * One fixed particle pool, no allocation after init; when it is full the oldest slot is reused. */
#include "bp.h"

#include <stdlib.h>
#include <string.h>

#define MAX_PARTS  400
#define MAX_PRIZES 6
#define PRIZE_Z    190.0f              /* prizes float in front of the whole balloon slab so they are always clear */
#define PRIZE_R    42.0f               /* world radius at that depth (about 52 px on screen) */
#define DT         PG_DT

enum { P_NONE, P_RECT, P_DOT, P_STAR, P_SPARK, P_RING, P_FLASH, P_SKIN };

typedef struct {
    uint8_t kind;
    float life, max_life, size, rot, rotv, ph, phv;
    PG_V3 p, v;
    PG_Color c;
} Part;

typedef struct { bool on; int kind; PG_V3 p; float age, life, ph, spin; } Prize;

static Part  P[MAX_PARTS];
static int   next_free;
static float pop_load;                                   /* decays; many pops in a short time use fewer pieces */
static Prize PZ[MAX_PRIZES];
static PG_Mesh *m_prize[3];
static PG_Tex t_prizes, t_sparkle, t_ring, t_hand;
static float prize_cd;

static struct { float alpha, t, tx, ty, tr; int tgt; uint32_t gen; bool rippled; } H;

/* -------------------------------------------------------------- 3D prizes */
/* An outline (CCW or CW, converted) extruded with a shallow pyramid on each face, flat shaded: a chunky
 * PS1-style gem. Planar texture mapping in object space. */
static void add_tri(Build *b, PG_V3 p0, PG_V3 p1, PG_V3 p2, PG_V3 n, bool face_n)
{
    PG_V3 g = pg_v3_cross(pg_v3_sub(p1, p0), pg_v3_sub(p2, p0));
    if (face_n) n = pg_v3_norm(g);
    else if (pg_v3_dot(g, n) < 0) { PG_V3 t = p1; p1 = p2; p2 = t; }
    PG_V3 q[3] = { p0, p1, p2 };
    int id[3];
    for (int i = 0; i < 3; i++) id[i] = build_vert(b, q[i].x, q[i].y, q[i].z, n.x, n.y, n.z, 0.5f + q[i].x * 0.42f, 0.5f - q[i].y * 0.42f, pg_rgb(255, 255, 255));
    build_tri(b, id[0], id[1], id[2]);
}

static void extrude(Build *b, const float *xy, int n, float depth, float apex, float scale, float ox, float oy, float rotz)
{
    float px[32], py[32], area = 0, cx = 0, cy = 0, c = cosf(rotz), s = sinf(rotz);
    for (int i = 0; i < n; i++) {
        float x = xy[i * 2] * scale, y = xy[i * 2 + 1] * scale;
        px[i] = ox + x * c - y * s; py[i] = oy + x * s + y * c;
        cx += px[i]; cy += py[i];
    }
    cx /= (float)n; cy /= (float)n;
    for (int i = 0; i < n; i++) { int j = (i + 1) % n; area += px[i] * py[j] - px[j] * py[i]; }
    if (area < 0) for (int i = 0; i < n / 2; i++) { float t = px[i]; px[i] = px[n - 1 - i]; px[n - 1 - i] = t; t = py[i]; py[i] = py[n - 1 - i]; py[n - 1 - i] = t; }
    PG_V3 cf = pg_v3(cx, cy, apex), cb = pg_v3(cx, cy, -apex);
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        PG_V3 a = pg_v3(px[i], py[i], depth), d = pg_v3(px[j], py[j], depth);
        PG_V3 a2 = pg_v3(px[i], py[i], -depth), d2 = pg_v3(px[j], py[j], -depth);
        add_tri(b, cf, a, d, pg_v3(0, 0, 1), true);
        add_tri(b, cb, d2, a2, pg_v3(0, 0, -1), true);
        float ex = px[j] - px[i], ey = py[j] - py[i], l = sqrtf(ex * ex + ey * ey);
        if (l < 1e-6f) continue;
        PG_V3 no = pg_v3(ey / l, -ex / l, 0);
        add_tri(b, a, d, d2, no, false);
        add_tri(b, a, d2, a2, no, false);
    }
}

static PG_Mesh *mesh_star(void)
{
    float xy[20];
    for (int k = 0; k < 10; k++) { float a = PG_PI * 0.5f + (float)k * PG_PI / 5.0f, r = (k & 1) ? 0.47f : 1.0f; xy[k * 2] = cosf(a) * r; xy[k * 2 + 1] = sinf(a) * r; }
    Build b = build_new(10 * 10 * 3, 12 * 10 * 3);
    extrude(&b, xy, 10, 0.16f, 0.36f, 1.0f, 0, 0.04f, 0);
    return build_done(&b);
}

static PG_Mesh *mesh_heart(void)
{
    float xy[40];
    for (int k = 0; k < 20; k++) {
        float t = PG_TAU * (float)k / 20.0f, x = 16.0f * powf(sinf(t), 3.0f);
        float y = 13.0f * cosf(t) - 5.0f * cosf(2 * t) - 2.0f * cosf(3 * t) - cosf(4 * t);
        xy[k * 2] = x / 17.0f; xy[k * 2 + 1] = (y + 2.5f) / 17.0f;
    }
    Build b = build_new(20 * 10 * 3, 12 * 20 * 3);
    extrude(&b, xy, 20, 0.17f, 0.40f, 1.0f, 0, 0.0f, 0);
    return build_done(&b);
}

static PG_Mesh *mesh_note(void)
{
    float head[28], stem[8] = { 0.10f, -0.60f, 0.27f, -0.60f, 0.27f, 0.98f, 0.10f, 0.98f };
    float flag[8] = { 0.27f, 0.98f, 0.70f, 0.62f, 0.70f, 0.28f, 0.27f, 0.66f };
    for (int k = 0; k < 14; k++) { float a = PG_TAU * (float)k / 14.0f; head[k * 2] = cosf(a) * 0.46f; head[k * 2 + 1] = sinf(a) * 0.34f; }
    Build b = build_new(3000, 4000);
    extrude(&b, head, 14, 0.15f, 0.26f, 1.0f, -0.30f, -0.62f, 0.45f);
    extrude(&b, stem, 4, 0.10f, 0.12f, 1.0f, 0, 0, 0);
    extrude(&b, flag, 4, 0.10f, 0.12f, 1.0f, 0, 0, 0);
    return build_done(&b);
}

void fx_prize(PG_V3 p)
{
    if (prize_cd > 0) return;
    int n = 0, slot = -1;
    for (int i = 0; i < MAX_PRIZES; i++) { if (PZ[i].on) n++; else if (slot < 0) slot = i; }
    if (slot < 0 || n >= 4) return;
    Prize *z = &PZ[slot];
    memset(z, 0, sizeof *z);
    float sx, sy, wx = p.x, wy = p.y;
    if (cam_project(p.x, p.y, p.z, &sx, &sy)) cam_unproject(sx, sy, PRIZE_Z, &wx, &wy);   /* same spot on screen, nearer the camera */
    p = pg_v3(wx, wy, PRIZE_Z);
    z->kind = pg_rand_int(0, 2);
    z->on = true; z->p = p; z->life = 3.6f; z->ph = frand(0, PG_TAU); z->spin = frand(0, PG_TAU);
    prize_cd = 0.25f;
    fx_burst(p, 50.0f, 10, 260.0f);
    snd_prize();
}

int fx_prize_hit(float sx, float sy)
{
    for (int i = 0; i < MAX_PRIZES; i++) {
        const Prize *z = &PZ[i];
        if (!z->on || z->age < 0.25f) continue;
        float bx, by;
        if (!cam_project(z->p.x, z->p.y, z->p.z, &bx, &by)) continue;
        float r = PRIZE_R * cam_scale(z->p.z) + 18.0f;
        if ((sx - bx) * (sx - bx) + (sy - by) * (sy - by) <= r * r) return i;
    }
    return -1;
}

void fx_prize_pop(int i)
{
    if (i < 0 || i >= MAX_PRIZES || !PZ[i].on) return;
    fx_burst(PZ[i].p, 40.0f + 80.0f * (float)PZ[i].kind, 14, 330.0f);
    fx_ripple(PZ[i].p, 120.0f, pg_rgb(255, 224, 90));
    snd_prize();
    PZ[i].on = false;
}

/* ---------------------------------------------------------------- particles */
static Part *part_new(uint8_t kind)
{
    Part *q = NULL;
    for (int k = 0; k < MAX_PARTS; k++) {
        int i = (next_free + k) % MAX_PARTS;
        if (P[i].kind == P_NONE) { q = &P[i]; next_free = (i + 1) % MAX_PARTS; break; }
    }
    if (!q) { q = &P[next_free]; next_free = (next_free + 1) % MAX_PARTS; }
    memset(q, 0, sizeof *q);
    q->kind = kind;
    return q;
}

static PG_Color party_color(int col)
{
    if (pg_randf() < 0.5f) return bal_color(col, 255);
    return bal_color(pg_rand_int(0, N_COL - 1), 255);
}

void fx_pop(PG_V3 p, int col, float r)
{
    float k = 1.0f / (1.0f + 0.16f * pop_load);
    pop_load += 1.0f;
    int n_rect = (int)(15.0f * k + 3.0f), n_dot = (int)(7.0f * k + 2.0f), n_star = (int)(4.0f * k + 1.0f), n_skin = (int)(6.0f * k + 2.0f);
    for (int i = 0; i < n_rect + n_dot + n_star + n_skin; i++) {
        uint8_t kind = i < n_rect ? P_RECT : i < n_rect + n_dot ? P_DOT : i < n_rect + n_dot + n_star ? P_STAR : P_SKIN;
        Part *q = part_new(kind);
        float a = frand(0, PG_TAU), sp = kind == P_SKIN ? frand(220, 420) : frand(130, 470);
        float rr = frand(0.1f, 0.6f) * r;
        q->p = pg_v3(p.x + cosf(a) * rr, p.y + sinf(a) * rr * 1.1f, p.z + frand(-r * 0.4f, r * 0.4f));
        q->v = pg_v3(cosf(a) * sp, sinf(a) * sp + 70.0f, frand(-150, 150));
        q->life = q->max_life = frand(1.2f, 2.0f);
        q->rot = frand(0, PG_TAU); q->rotv = frand(-7, 7); q->ph = frand(0, PG_TAU); q->phv = frand(6, 13);
        q->size = kind == P_RECT ? frand(13, 21) : kind == P_DOT ? frand(8, 13) : kind == P_STAR ? frand(15, 24) : frand(20, 30);
        q->c = kind == P_SKIN ? bal_color(col, 255) : kind == P_STAR ? pg_color_lerp(party_color(col), pg_rgb(255, 255, 255), 0.25f) : party_color(col);
    }
    Part *g = part_new(P_RING);                       /* shock ring in the balloon's colour */
    g->p = pg_v3(p.x, p.y, p.z + r * 0.3f); g->life = g->max_life = 0.38f; g->size = r * 3.3f; g->c = bal_color(col, 255);
    g = part_new(P_FLASH);                            /* the big happy sparkle */
    g->p = pg_v3(p.x, p.y, p.z + r * 0.5f); g->life = g->max_life = 0.5f; g->size = r * 3.0f; g->rot = frand(0, 1); g->rotv = 1.2f;
    g->c = pg_rgb(255, 214, 70);
    for (int i = 0; i < 5; i++) {
        float a = frand(0, PG_TAU), rr = frand(0.7f, 1.3f) * r;
        Part *s = part_new(P_SPARK);
        s->p = pg_v3(p.x + cosf(a) * rr, p.y + sinf(a) * rr, p.z + r * 0.5f);
        s->v = pg_v3(cosf(a) * 40, sinf(a) * 40, 0);
        s->life = s->max_life = frand(0.45f, 0.8f); s->size = frand(20, 34); s->rot = frand(0, 1);
        s->c = pg_hsv(frand(20, 60), 0.55f, 1.0f);
    }
}

void fx_burst(PG_V3 p, float hue, int n, float speed)
{
    for (int i = 0; i < n; i++) {
        float a = frand(0, PG_TAU), sp = frand(0.35f, 1.0f) * speed;
        Part *s = part_new(P_SPARK);
        s->p = p; s->v = pg_v3(cosf(a) * sp, sinf(a) * sp, frand(-30, 30));
        s->life = s->max_life = frand(0.4f, 0.8f); s->size = frand(16, 30); s->rot = frand(0, 1);
        s->c = pg_hsv(hue + frand(-30, 30), 0.5f, 1.0f);
    }
}

void fx_trail(PG_V3 p, float hue)
{
    Part *s = part_new(P_SPARK);
    s->p = pg_v3(p.x + frand(-6, 6), p.y + frand(-6, 6), p.z);
    s->v = pg_v3(frand(-25, 25), frand(-10, 40), 0);
    s->life = s->max_life = frand(0.35f, 0.55f); s->size = frand(18, 28); s->rot = frand(0, 1);
    s->c = pg_hsv(hue + frand(-25, 25), 0.55f, 1.0f);
}

void fx_ripple(PG_V3 p, float size, PG_Color c)
{
    Part *g = part_new(P_RING);
    g->p = p; g->life = g->max_life = 0.5f; g->size = size; g->c = c;
}

void fx_init(void)
{
    t_sparkle = tex_upload(paint_sparkle(), false);
    t_ring = tex_upload(paint_ring(), false);
    t_hand = tex_upload(paint_hand(), true);
    t_prizes = tex_upload(paint_prizes(), true);
    m_prize[0] = mesh_star(); m_prize[1] = mesh_heart(); m_prize[2] = mesh_note();
    memset(P, 0, sizeof P); memset(PZ, 0, sizeof PZ); memset(&H, 0, sizeof H);
    next_free = 0; pop_load = 0; prize_cd = 0; H.tgt = -1;
}

void fx_free(void)
{
    for (int k = 0; k < 3; k++) { pg_mesh_free(m_prize[k]); m_prize[k] = NULL; }
}

void fx_update(void)
{
    pop_load *= 0.985f;
    if (prize_cd > 0) prize_cd -= DT;
    for (int i = 0; i < MAX_PARTS; i++) {
        Part *q = &P[i];
        if (q->kind == P_NONE) continue;
        float age = q->max_life - q->life;
        if (q->kind == P_RECT || q->kind == P_DOT || q->kind == P_STAR || q->kind == P_SKIN) {
            float drag = lerpf(0.9f, 4.2f, smooth01(age / 0.55f)), f = 1.0f / (1.0f + drag * DT);
            q->v.x *= f; q->v.y *= f; q->v.z *= f;
            q->v.y -= 330.0f * DT;
            if (q->kind != P_DOT) q->v.x += sinf(q->ph * 0.5f + age * 6.0f) * 70.0f * DT;
            q->ph += q->phv * DT; q->rot += q->rotv * DT;
        } else if (q->kind == P_SPARK) {
            float f = 1.0f / (1.0f + 3.0f * DT);
            q->v.x *= f; q->v.y *= f;
            q->rot += 0.8f * DT;
        } else if (q->kind == P_FLASH) q->rot += q->rotv * DT;
        q->p.x += q->v.x * DT; q->p.y += q->v.y * DT; q->p.z += q->v.z * DT;
        q->life -= DT;
        if (q->life <= 0) q->kind = P_NONE;
    }
    for (int i = 0; i < MAX_PRIZES; i++) {
        Prize *z = &PZ[i];
        if (!z->on) continue;
        z->age += DT;
        z->spin += (3.4f - 1.5f * smooth01(z->age / 3.0f)) * DT;
        /* a short float upwards that settles into a gentle hover; never leaves the screen at the top */
        z->p.y += (95.0f * expf(-z->age * 2.4f) + 6.0f * cosf(z->age * 2.6f + z->ph)) * DT;
        z->p.x += sinf(z->age * 1.8f + z->ph) * 22.0f * DT;
        float sx, sy;
        if (cam_project(z->p.x, z->p.y, z->p.z, &sx, &sy) && sy < 105.0f) z->p.y -= (105.0f - sy) * 0.08f / cam_scale(z->p.z);
        if (((int)(z->age * 60.0f) % 7) == 0) fx_trail(pg_v3(z->p.x + frand(-14, 14), z->p.y - 14, z->p.z + 10), 50.0f + 80.0f * (float)z->kind);
        if (z->age >= z->life) z->on = false;
    }
}

static PG_Color shade(PG_Color c, float k, float a)
{
    return pg_rgba((uint8_t)clampf((float)c.r * k, 0, 255), (uint8_t)clampf((float)c.g * k, 0, 255), (uint8_t)clampf((float)c.b * k, 0, 255),
                   (uint8_t)clampf(a * 255.0f, 0, 255));
}

void fx_draw(void)
{
    PG_Tex white = pg3d_white(), disc = pg3d_builtin(PG3D_TEX_DISC), star = pg3d_builtin(PG3D_TEX_STAR);
    for (int i = 0; i < MAX_PARTS; i++) {
        const Part *q = &P[i];
        if (q->kind == P_NONE) continue;
        float prog = 1.0f - q->life / q->max_life;
        float fade = q->life > 0.4f ? 1.0f : q->life / 0.4f;
        switch (q->kind) {
        case P_RECT: case P_SKIN: {
            float flip = cosf(q->ph), af = fabsf(flip);
            float w = q->size * (0.14f + 0.86f * af), h = q->size * (q->kind == P_SKIN ? 0.8f : 0.58f);
            pg3d_sprite(white, q->p, w, h, q->rot, shade(q->c, 0.66f + 0.34f * af, fade), 0);
            break; }
        case P_DOT:
            pg3d_sprite(disc, q->p, q->size, q->size, 0, shade(q->c, 1.0f, fade), 0);
            break;
        case P_STAR:
            pg3d_sprite(star, q->p, q->size * (0.35f + 0.65f * fabsf(cosf(q->ph * 0.8f))), q->size, q->rot, shade(q->c, 1.0f, fade), 0);
            break;
        case P_SPARK: {
            float s = q->size * (q->life / q->max_life > 0.5f ? 1.0f : 2.0f * q->life / q->max_life * 0.9f + 0.1f);
            pg3d_sprite(t_sparkle, q->p, s, s, q->rot, shade(q->c, 1.0f, 1.0f), 0);
            break; }
        case P_RING: {
            float s = q->size * (0.35f + 0.65f * smooth01(prog * 1.4f));
            pg3d_sprite(t_ring, q->p, s, s, 0, shade(q->c, 1.0f, (1.0f - prog) * 0.95f), 0);
            break; }
        case P_FLASH: {
            float grow = prog < 0.35f ? smooth01(prog / 0.35f) : 1.0f - 0.25f * smooth01((prog - 0.35f) / 0.65f);
            float s = q->size * grow * (0.4f + 0.6f * grow), a = prog < 0.5f ? 1.0f : 1.0f - smooth01((prog - 0.5f) / 0.5f);
            pg3d_sprite(t_sparkle, q->p, s, s, q->rot, shade(q->c, 1.0f, a), 0);
            pg3d_sprite(t_sparkle, pg_v3(q->p.x, q->p.y, q->p.z + 2.0f), s * 0.62f, s * 0.62f, -q->rot * 0.7f, pg_rgba(255, 255, 255, (uint8_t)(a * 255.0f)), PG3D_ADDITIVE);
            break; }
        default: break;
        }
    }
    for (int i = 0; i < MAX_PRIZES; i++) {
        const Prize *z = &PZ[i];
        if (!z->on) continue;
        float in = smooth01(z->age / 0.3f), over = 1.0f + 0.28f * sinf(PG_PI * clampf(z->age / 0.3f, 0, 1));
        float out = z->age > z->life - 0.8f ? (z->life - z->age) / 0.8f : 1.0f;
        float s = PRIZE_R * in * over * (0.4f + 0.6f * out);
        PG_M4 m = pg_m4_mul(pg_m4_translate(z->p.x, z->p.y, z->p.z), pg_m4_mul(pg_m4_rot_y(z->spin), pg_m4_mul(pg_m4_rot_z(0.14f * sinf(z->age * 2.3f + z->ph)), pg_m4_scale(s, s, s))));
        pg3d_mesh(m_prize[z->kind], tex_sub(t_prizes, z->kind * 32, 0, 32, 32, 96, 32), &m, pg_rgba(255, 255, 255, (uint8_t)(out * 255.0f)), 0);
        float halo = 112.0f * in * (0.92f + 0.08f * sinf(z->age * 6.0f));          /* soft warm glow BEHIND the prize (alpha blended) */
        pg3d_sprite(pg3d_builtin(PG3D_TEX_GLOW), pg_v3(z->p.x, z->p.y, z->p.z - 30.0f), halo, halo, 0, pg_rgba(255, 214, 96, (uint8_t)(190.0f * out)), 0);
    }
}

/* ------------------------------------------------------------ touch glow */
void fx_touch_glow(const PG_Touch *t, float strength)
{
    const float z = 230.0f;
    const Cam *cm = cam_get();
    float wx, wy;
    cam_unproject(t->x, t->y, z, &wx, &wy);
    float k = (cm->dist - z) / cm->focal;
    float pulse = 0.5f + 0.5f * sinf(pg_time() * 7.0f + (float)t->id), pop = smooth01((float)(t->ticks + 1) / 7.0f) * strength;
    PG_Color hc = pg_hsv(fmodf(30.0f + 47.0f * (float)t->id, 360.0f), 0.62f, 1.0f);
    float g = 168.0f * k * (0.85f + 0.15f * pulse) * pop, rg = 82.0f * k * (1.0f + 0.07f * pulse) * pop;
    pg3d_sprite(pg3d_builtin(PG3D_TEX_GLOW), pg_v3(wx, wy, z), g, g, 0, pg_rgba(hc.r, hc.g, hc.b, (uint8_t)(170.0f * strength)), 0);
    pg3d_sprite(t_ring, pg_v3(wx, wy, z + 1.0f), rg, rg, 0, pg_rgba(255, 255, 255, (uint8_t)(235.0f * strength)), 0);
    pg3d_sprite(pg3d_builtin(PG3D_TEX_GLOW), pg_v3(wx, wy, z + 2.0f), 74.0f * k * pop, 74.0f * k * pop, 0, pg_rgba(255, 255, 255, (uint8_t)(190.0f * strength)), PG3D_ADDITIVE);
}

/* -------------------------------------------------------------- hint hand */
/* The hand glides in from the lower right, taps the balloon twice (a ripple each, no pop), hovers, then
 * glides out again. Between cycles it re-picks a balloon that is comfortably on screen. */
#define HINT_CYCLE 2.5f

static float hint_near(float ph)                    /* 1 = far away (lower right), 0 = on the balloon */
{
    if (ph < 0.55f) return 1.0f - smooth01(ph / 0.55f);
    if (ph < 1.85f) return 0.0f;
    return smooth01((ph - 1.85f) / 0.65f);
}

static float hint_dip(float ph)                     /* 1 = pressed in */
{
    static const float at[2] = { 0.55f, 1.15f };
    for (int i = 0; i < 2; i++)
        if (ph >= at[i] && ph < at[i] + 0.25f) return ph < at[i] + 0.11f ? smooth01((ph - at[i]) / 0.11f) : 1.0f - smooth01((ph - at[i] - 0.11f) / 0.14f);
    return 0.0f;
}

void hint_update(float idle_s, bool first_time)
{
    bool want = idle_s >= (first_time ? 2.2f : 5.0f);
    float prev = fmodf(H.t, HINT_CYCLE);
    if (want) { H.alpha = fminf(1.0f, H.alpha + DT * 3.0f); H.t += DT; }
    else { H.alpha = fmaxf(0.0f, H.alpha - DT * 6.0f); if (H.alpha <= 0) { H.t = 0; H.tgt = -1; H.tr = 0; } }
    if (H.alpha <= 0) return;
    float ph = fmodf(H.t, HINT_CYCLE);
    if (H.tgt < 0 || !balloon_alive(H.tgt, H.gen) || ph < prev) {          /* appearing, target gone, or a new cycle */
        int pick = balloon_for_hint(H.tgt, H.gen);
        if (pick != H.tgt || !balloon_alive(H.tgt, H.gen)) { H.tgt = pick; H.gen = balloon_gen(pick); }
    }
    float sx, sy, sr;
    if (H.tgt >= 0 && balloon_screen(H.tgt, &sx, &sy, &sr)) {
        if (H.tr <= 0) { H.tx = sx; H.ty = sy; }
        H.tx += (sx - H.tx) * 0.2f; H.ty += (sy - H.ty) * 0.2f; H.tr = sr;
    }
    static const float rip[2] = { 0.64f, 1.24f };
    for (int i = 0; i < 2; i++)
        if (H.tgt >= 0 && prev < rip[i] && ph >= rip[i]) {
            float wx, wy;
            cam_unproject(H.tx, H.ty + H.tr * 0.1f, 40.0f, &wx, &wy);
            fx_ripple(pg_v3(wx, wy, 40.0f), 110.0f, pg_rgb(255, 255, 255));
        }
}

void hint_draw(void)
{
    if (H.alpha <= 0.01f || H.tgt < 0 || H.tr <= 0) return;
    const float W = 118.0f;
    float ph = fmodf(H.t, HINT_CYCLE), near_px = hint_near(ph), dip = hint_dip(ph);
    float bob = near_px == 0.0f && dip == 0.0f ? sinf(H.t * 4.0f) * 3.0f : 0.0f;
    float tipx = H.tx + H.tr * 0.05f, tipy = H.ty + H.tr * 0.12f;
    float cx = tipx + (0.5f - 0.31f) * W + near_px * 130.0f - dip * 10.0f;
    float cy = tipy + (0.5f - 0.10f) * W + near_px * 120.0f + bob - dip * 10.0f;
    const float z = 260.0f;
    float wx, wy;
    cam_unproject(cx, cy, z, &wx, &wy);
    float wk = (cam_get()->dist - z) / cam_get()->focal, sz = W * wk * (1.0f - 0.06f * dip);
    uint8_t a = (uint8_t)(H.alpha * 255.0f);
    pg3d_sprite(pg3d_builtin(PG3D_TEX_SHADOW), pg_v3(wx + 7.0f * wk, wy - 9.0f * wk, z - 1.0f), sz * 0.95f, sz * 0.8f, 0, pg_rgba(255, 255, 255, (uint8_t)(H.alpha * 90.0f)), 0);
    pg3d_sprite(t_hand, pg_v3(wx, wy, z), sz, sz, 0, pg_rgba(255, 255, 255, a), 0);
}
