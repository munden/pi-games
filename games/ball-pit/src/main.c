/* ball-pit - a giant toy-box ball pit for a 4-year-old.
 *
 * Physics lives in the z=0 plane (pg_phys circles); everything is drawn as real lit, textured 3D: a padded tub in a
 * playroom, shiny painted balls that roll and squash, a rainbow arch, bunting and a swaying camera.
 *
 * How it plays (no reading, nothing can go wrong):
 *   - every finger is a big invisible-physics circle with a glowing ring: it scoops, shoves and flings balls;
 *   - a tap on a ball pops the balls around it into the air;
 *   - a tap (or a hold) on empty space makes balls drop from the beam, playing a little pentatonic tune that
 *     rises from left to right;
 *   - at most MAX_BALLS balls exist: the oldest one pops out in a puff of stars, so the pit never fills up;
 *   - left alone, balls rain gently and a pointing hand shows what to do.
 *
 * Physics bodies are never added or removed while playing: every ball, finger and the hint hand owns a body made at
 * init that is "parked" (made static and moved inside the floor, where nothing can touch it) while it is not in use.
 * Recycling slots would let the engine warm-start a new contact with the leftover impulse of the old one.
 */
#include "pit.h"

#include <math.h>
#include <string.h>

/* ------------------------------------------------------------------------------------------------ tuning */
#define MAX_BALLS     70
#define POOL          (MAX_BALLS + 14)        /* live balls + balls that are playing their pop-out animation */
#define MAX_SPARKS    120
#define BACK_BALLS    36
#define FINGER_R      50.0f
#define FINGER_VMAX   1100.0f
#define GRAVITY      (-950.0f)
#define IDLE_BALLS    20
#define DROP_Q        24
#define N_FLAGS       13
#define MAX_SPEED     1900.0f                 /* nothing in the pit ever moves faster than this */

enum { TAG_BALL = 1, TAG_WALL, TAG_FINGER };
enum { B_FREE = 0, B_LIVE, B_DYING };
enum { SP_STAR, SP_RING };
enum { CAT_BB, CAT_BW, CAT_BF, CAT_N };

typedef struct {
    uint8_t  state, tex, lod;
    PG_Color tint;
    PG_Body *body;
    float    r, x, y, ang, tilt, ct, st, yaw0, hue;
    float    age;                          /* seconds since it appeared (pop-in animation) */
    float    sq, sq_t, sqx, sqy;           /* squash amplitude, its clock and the impact direction */
    float    die_t;
    uint32_t seq;                          /* creation order: the lowest one is the oldest */
} Ball;

typedef struct { PG_V3 p, v; float life, max, size, hue, spin; uint8_t kind; } Spark;

typedef struct {
    PG_Body *body;
    bool     held, pour;
    int      linger;
    float    wx, wy;                       /* where the finger touches the z=0 plane */
    float    pour_cd, trail;
} Finger;

typedef struct { bool active; float x, delay, wait, vol; int note; } Drop;
typedef struct { float x, y, z, r, a0, tilt, spin; uint8_t tex, lod; PG_Color tint; } BackBall;

/* ------------------------------------------------------------------------------------------------ state */
static PitTex    T;
static PG_Mesh  *m_sph[4];                /* unit spheres: 0 large (14x8), 1 medium (12x7), 2 small (10x6), 3 backdrop (10x5) */
static PG_Mesh  *m_paper, *m_dado, *m_side[2], *m_floor, *m_arch, *m_lip_front, *m_lip_top, *m_lip_cham, *m_ceil, *m_pennant;
static PG_World *W;
static Ball      balls[POOL];
static Finger    fing[PG_MAX_TOUCH];
static Spark     sparks[MAX_SPARKS];
static Drop      dq[DROP_Q];
static BackBall  back[BACK_BALLS];
static int       n_back, live_cap = MAX_BALLS, recent[4] = { -1, -1, -1, -1 };
static uint32_t  seq_counter;
static uint64_t  chime_tick;
static float     idle_t = 7.5f, rain_cd = 1.0f, spawn_gate;
static float     hint_a, hint_x, hint_y, hint_phase;
static PG_Body  *hint_body;
static bool      hint_on, headless;
static float     snd_best[CAT_N], snd_r[CAT_N];
static uint32_t  snd_last[CAT_N];
static PG_Camera cam_now;

static const PG_Color PALETTE[8] = {
    { 255, 66, 70, 255 }, { 255, 150, 34, 255 }, { 255, 214, 44, 255 }, { 72, 214, 98, 255 },
    { 44, 200, 208, 255 }, { 70, 130, 255, 255 }, { 170, 100, 255, 255 }, { 255, 112, 186, 255 } };

/* ------------------------------------------------------------------------------------------------ helpers */
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float smooth01(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }
static float ease_out_back(float t) { t = clampf(t, 0, 1) - 1.0f; return 1.0f + 2.70158f * t * t * t + 1.70158f * t * t; }

static int live_count(void)
{
    int n = 0;
    for (int i = 0; i < POOL; i++) n += balls[i].state == B_LIVE;
    return n;
}

static Ball *oldest_live(void)
{
    Ball *o = NULL;
    for (int i = 0; i < POOL; i++)
        if (balls[i].state == B_LIVE && (!o || balls[i].seq < o->seq)) o = &balls[i];
    return o;
}

/* Park a body: static, still, and inside the floor slab, one cell per slot so the broad phase stays cheap. */
static void park(PG_Body *b, int slot)
{
    b->inv_mass = 0; b->inv_inertia = 0;
    b->vx = b->vy = b->w = 0;
    b->x = -400.0f + (float)(slot % 9) * 100.0f;
    b->y = FLOOR_Y - 150.0f - (float)(slot / 9) * 100.0f;
}

/* ------------------------------------------------------------------------------------------------ sparkles */
static void spark_add(int kind, float x, float y, float z, float vx, float vy, float life, float size, float hue)
{
    for (int i = 0; i < MAX_SPARKS; i++) {
        if (sparks[i].life > 0) continue;
        sparks[i] = (Spark){ pg_v3(x, y, z), pg_v3(vx, vy, 0), life, life, size, hue, pg_rand_range(-3, 3), (uint8_t)kind };
        return;
    }
}

static void burst(float x, float y, int n, float hue, float speed)
{
    for (int i = 0; i < n; i++) {
        float a = pg_rand_range(0, PG_TAU), s = pg_rand_range(0.35f, 1.0f) * speed;
        spark_add(SP_STAR, x, y, 40, cosf(a) * s, sinf(a) * s + speed * 0.35f, pg_rand_range(0.55f, 1.0f),
                  pg_rand_range(16, 28), hue + pg_rand_range(-50, 50));
    }
}

static void sparks_update(void)
{
    for (int i = 0; i < MAX_SPARKS; i++) {
        Spark *s = &sparks[i];
        if (s->life <= 0) continue;
        s->life -= PG_DT;
        if (s->kind == SP_STAR) {
            s->v.y -= 430.0f * PG_DT;
            s->v.x *= 1.0f - 1.2f * PG_DT; s->v.y *= 1.0f - 0.6f * PG_DT;
            s->p = pg_v3_add(s->p, pg_v3_scale(s->v, PG_DT));
        }
    }
}

/* ------------------------------------------------------------------------------------------------ balls */
static float pick_radius(void)
{
    float k = pg_randf();
    if (k < 0.36f) return pg_rand_range(22, 27);
    if (k < 0.74f) return pg_rand_range(30, 37);
    return pg_rand_range(40, 46);
}

static void pick_look(uint8_t *tex, PG_Color *tint, float *hue)
{
    for (int tries = 0; tries < 10; tries++) {
        int kind = pg_randf() < 0.38f ? BT_PLAIN : pg_rand_int(1, BT_COUNT - 1);
        int ti = pg_rand_int(0, 7), key = kind == BT_PLAIN ? 100 + ti : kind;
        bool dup = false;
        for (int k = 0; k < 4; k++) dup |= recent[k] == key;
        if (dup && tries < 9) continue;
        memmove(recent + 1, recent, 3 * sizeof recent[0]);
        recent[0] = key;
        *tex = (uint8_t)kind;
        *tint = kind == BT_PLAIN ? PALETTE[ti] : pg_rgb(255, 255, 255);
        *hue = kind == BT_PLAIN ? (float)ti * 45.0f : pg_rand_range(0, 360);
        return;
    }
}

static int lod_for(float r) { return r < 28.0f ? 2 : r < 38.0f ? 1 : 0; }

static void ball_pop(Ball *b, bool sound)
{
    if (b->state != B_LIVE) return;
    b->x = b->body->x; b->y = b->body->y; b->ang = b->body->angle;
    park(b->body, (int)(b - balls));
    b->state = B_DYING; b->die_t = 0;
    burst(b->x, b->y, 7, b->hue, 220);
    if (sound) pg_sfx_pop(0.30f);
}

static bool free_spot(float x, float y, float r)
{
    for (int i = 0; i < POOL; i++) {
        const Ball *o = &balls[i];
        if (o->state != B_LIVE) continue;
        float dx = o->body->x - x, dy = o->body->y - y, rr = o->r + r + 3.0f;
        if (dx * dx + dy * dy < rr * rr) return false;
    }
    return true;
}

static bool finger_near(float x, float y, float r)
{
    for (int i = 0; i < PG_MAX_TOUCH; i++) {
        if (!fing[i].held && fing[i].linger <= 0) continue;
        float dx = fing[i].body->x - x, dy = fing[i].body->y - y, rr = FINGER_R + r + 4.0f;
        if (dx * dx + dy * dy < rr * rr) return true;
    }
    if (hint_on) {
        float dx = hint_body->x - x, dy = hint_body->y - y, rr = hint_body->radius + r + 4.0f;
        if (dx * dx + dy * dy < rr * rr) return true;
    }
    return false;
}

/* A new ball only appears where it has room to fall: no stack of balls right below it, pinned against the beam. */
static bool spawn_clear(float x, float y, float r)
{
    if (finger_near(x, y, r)) return false;
    for (int k = 0; k < 3; k++)
        if (!free_spot(x, y - 1.7f * r * (float)k, r)) return false;
    return true;
}

static Ball *ball_new(float x, float y, float vx, float vy, float r, bool pop_in)
{
    while (live_count() >= live_cap) {                  /* the pit never fills up: the oldest ball pops out gracefully */
        Ball *o = oldest_live();
        if (!o) break;
        ball_pop(o, true);
    }
    Ball *b = NULL;
    for (int i = 0; i < POOL && !b; i++) if (balls[i].state == B_FREE) b = &balls[i];
    if (!b) return NULL;
    PG_Body *bd = b->body;                              /* the slot's own body, waking up */
    float mass = PG_PI * r * r;
    bd->radius = r; bd->inv_mass = 1.0f / mass; bd->inv_inertia = 1.0f / (0.5f * mass * r * r);
    bd->x = x; bd->y = y; bd->angle = 0;
    bd->vx = vx; bd->vy = vy; bd->w = pg_rand_range(-3, 3);
    b->state = B_LIVE; b->r = r; b->lod = (uint8_t)lod_for(r);
    b->x = x; b->y = y; b->ang = 0;
    b->sq = b->sq_t = 0; b->die_t = 0;
    pick_look(&b->tex, &b->tint, &b->hue);
    b->tilt = pg_rand_range(-0.6f, 0.6f); b->ct = cosf(b->tilt); b->st = sinf(b->tilt);
    b->yaw0 = pg_rand_range(0, PG_TAU);
    b->age = pop_in ? 0.0f : 1.0f;
    b->seq = ++seq_counter;
    return b;
}

static void squash(Ball *b, float cx, float cy, float speed)
{
    float amp = clampf((speed - 160.0f) / 2600.0f, 0, 0.17f);
    if (amp < 0.025f || b->state != B_LIVE) return;
    if (amp < b->sq * expf(-8.0f * b->sq_t)) return;
    float nx = cx - b->body->x, ny = cy - b->body->y, l = sqrtf(nx * nx + ny * ny);
    if (l < 1e-3f) { nx = 0; ny = -1; } else { nx /= l; ny /= l; }
    b->sq = amp; b->sq_t = 0; b->sqx = nx; b->sqy = ny;
}

static float pile_top(float x)
{
    float top = FLOOR_Y;
    for (int i = 0; i < POOL; i++) {
        const Ball *b = &balls[i];
        if (b->state != B_LIVE || fabsf(b->body->x - x) > b->r + 40.0f) continue;
        if (b->body->y + b->r > top) top = b->body->y + b->r;
    }
    return top;
}

/* ------------------------------------------------------------------------------------------------ contacts and sound */
static void on_contact(PG_Body *a, PG_Body *b, float speed, float x, float y, void *ud)
{
    (void)ud;
    if (a->tag != TAG_BALL) { PG_Body *t = a; a = b; b = t; }
    if (a->tag != TAG_BALL) return;
    Ball *ba = a->user;
    int cat = b->tag == TAG_BALL ? CAT_BB : b->tag == TAG_WALL ? CAT_BW : CAT_BF;
    squash(ba, x, y, speed);
    if (b->tag == TAG_BALL) squash(b->user, x, y, speed);
    if (speed > snd_best[cat]) { snd_best[cat] = speed; snd_r[cat] = ba->r; }
    if (speed > 640.0f) burst(x, y, 3, ba->hue, 170);
}

static void sounds_play(void)
{
    static const uint32_t gap[CAT_N] = { 5, 7, 6 };         /* ticks between sounds of one kind */
    uint32_t now = (uint32_t)pg_ticks();
    for (int c = 0; c < CAT_N; c++) {
        float sp = snd_best[c];
        if (sp <= 0 || now - snd_last[c] < gap[c]) continue;
        float pitch = (1.3f - (snd_r[c] - 22.0f) / 24.0f * 0.55f) * (0.95f + 0.1f * pg_randf());
        if (c == CAT_BB) {
            if (sp < 110) continue;
            pg_sfx_tap(clampf(0.10f + (sp - 110.0f) / 1400.0f, 0.10f, 0.42f));
        } else if (c == CAT_BW) {
            if (sp < 190) continue;
            float vol = clampf(0.16f + (sp - 190.0f) / 1500.0f, 0.16f, 0.50f);
            if (snd_r[c] > 36.0f && sp > 560) pg_sfx_thud(vol * 0.55f); else pg_sfx_boing(vol, pitch);
        } else {
            if (sp < 150) continue;
            pg_sfx_boing(clampf(0.20f + (sp - 150.0f) / 1500.0f, 0.2f, 0.5f), pitch * 1.15f);
        }
        snd_last[c] = now;
    }
}

/* ------------------------------------------------------------------------------------------------ drops */
static void queue_drop(float x, float delay, int note, float vol)
{
    for (int i = 0; i < DROP_Q; i++) {
        if (dq[i].active) continue;
        dq[i] = (Drop){ true, x, delay, 0, vol, note };
        return;
    }
}

static int note_for_x(float x) { return (int)clampf((x + WALL_X) / (2.0f * WALL_X) * 10.0f, 0, 9); }

static void drops_update(void)
{
    spawn_gate -= PG_DT;
    for (int i = 0; i < DROP_Q; i++) {
        Drop *d = &dq[i];
        if (!d->active) continue;
        d->delay -= PG_DT;
        if (d->delay > 0 || spawn_gate > 0) continue;
        float r = pick_radius(), y = CEIL_Y - r - 8, x = 0;
        bool ok = false;
        static const float side[5] = { 0, 1.0f, -1.0f, 2.1f, -2.1f };      /* blocked (a stack under the finger)? drop beside it */
        for (int k = 0; k < 5 && !ok; k++) {
            x = clampf(d->x + side[k] * (r + 10.0f) + pg_rand_range(-6, 6), -WALL_X + r + 8, WALL_X - r - 8);
            ok = spawn_clear(x, y, r);
        }
        if (!ok) {                                       /* really full up there: try again for a moment */
            d->wait += PG_DT;
            if (d->wait > 0.8f) d->active = false;
            continue;
        }
        if (ball_new(x, y, pg_rand_range(-30, 30), -260.0f, r, true)) {
            if (d->vol > 0 && pg_ticks() - chime_tick >= 7) { pg_sfx_chime(d->note, d->vol); chime_tick = pg_ticks(); }
            spark_add(SP_RING, x, y, 30, 0, 0, 0.45f, 120, 50);
            for (int k = 0; k < 3; k++) spark_add(SP_STAR, x, y, 40, pg_rand_range(-120, 120), pg_rand_range(-60, 90), 0.7f, 20, pg_rand_range(0, 360));
        }
        d->active = false;
        spawn_gate = 0.11f;
    }
}

static void rain_update(void)
{
    rain_cd -= PG_DT;
    if (rain_cd > 0) return;
    int live = live_count();
    if (live < IDLE_BALLS) {                              /* the pit fills itself gently */
        queue_drop(pg_rand_range(-WALL_X + 70, WALL_X - 70), 0, pg_rand_int(0, 9), 0.22f);
        rain_cd = 1.2f;
    } else if (idle_t > 12.0f) {                          /* nobody there: keep a slow trickle of new balls going */
        if (live >= IDLE_BALLS + 4) { Ball *o = oldest_live(); if (o) ball_pop(o, true); }
        queue_drop(pg_rand_range(-WALL_X + 70, WALL_X - 70), 0, pg_rand_int(0, 9), 0.2f);
        rain_cd = 5.0f;
    } else rain_cd = 0.5f;
}

/* ------------------------------------------------------------------------------------------------ fingers */
static bool touching_ball(float x, float y)
{
    for (int i = 0; i < POOL; i++) {
        const Ball *b = &balls[i];
        if (b->state != B_LIVE) continue;
        float dx = b->body->x - x, dy = b->body->y - y, rr = b->r + 30.0f;
        if (dx * dx + dy * dy < rr * rr) return true;
    }
    return false;
}

static void poke(float fx, float fy)                      /* a tap on the pile: the balls around hop into the air */
{
    int hit = 0;
    for (int i = 0; i < POOL; i++) {
        Ball *b = &balls[i];
        if (b->state != B_LIVE) continue;
        float dx = b->body->x - fx, dy = b->body->y - fy, d = sqrtf(dx * dx + dy * dy), reach = FINGER_R + b->r + 8.0f;
        if (d >= reach) continue;
        float k = 1.0f - d / reach, nx = d > 1 ? dx / d : 0.0f, ny = d > 1 ? dy / d : 1.0f, sp = 240.0f + 320.0f * k;
        b->body->vx += nx * sp * 0.8f;
        b->body->vy += ny * sp * 0.8f + 190.0f + 130.0f * k;
        b->body->w += pg_rand_range(-6, 6);
        squash(b, b->body->x - nx * b->r, b->body->y - ny * b->r, 600.0f + 900.0f * k);
        hit++;
    }
    if (hit) {
        pg_sfx_boing(0.42f, 0.95f + 0.1f * pg_randf());
        burst(fx, fy, 4, pg_rand_range(0, 360), 200);
    }
}

static void finger_step(int i, const PG_Touch *t)
{
    Finger *f = &fing[i];
    bool live = t->state == PG_TOUCH_DOWN || t->state == PG_TOUCH_HELD;
    if (live) {
        PG_V3 w;
        if (pg3d_plane_z(t->x, t->y, 0, &w)) { f->wx = w.x; f->wy = w.y; }
        f->held = true;
        float tx = clampf(f->wx, -WALL_X + 14.0f, WALL_X - 14.0f), ty = clampf(f->wy, FLOOR_Y + 14.0f, CEIL_Y - 14.0f);
        if (t->state == PG_TOUCH_DOWN) {
            f->linger = 0; f->trail = 0;
            f->pour = !touching_ball(tx, ty);
            f->body->x = tx; f->body->y = ty; f->body->vx = f->body->vy = 0;     /* the circle appears under the finger */
            spark_add(SP_RING, tx, ty, 30, 0, 0, 0.5f, 130, 190 + 40 * (float)i);
            if (f->pour) {                                /* empty space: three balls drop in a little rising tune */
                int n0 = note_for_x(tx);
                pg_sfx_chime(n0, 0.50f);                  /* heard on the very next frame; the balls follow */
                chime_tick = pg_ticks();
                for (int k = 0; k < 3; k++) {
                    int note = n0 + k * 2 > 9 ? 9 - (n0 + k * 2 - 9) % 3 : n0 + k * 2;
                    queue_drop(tx, 0.13f * (float)k, note, k ? 0.50f : 0.0f);
                }
                f->pour_cd = 0.75f;
            } else {
                poke(tx, ty);
                f->pour_cd = 1e9f;
            }
        } else {
            float vx = (tx - f->body->x) / PG_DT, vy = (ty - f->body->y) / PG_DT, sp = sqrtf(vx * vx + vy * vy);
            if (sp > FINGER_VMAX) { vx *= FINGER_VMAX / sp; vy *= FINGER_VMAX / sp; }
            f->body->vx = vx; f->body->vy = vy;
            f->trail += sp * PG_DT;
            if (f->trail > 70.0f) { f->trail = 0; spark_add(SP_STAR, f->wx, f->wy, 50, pg_rand_range(-40, 40), pg_rand_range(-20, 60), 0.55f, 18, 40.0f + 55.0f * (float)i); }
        }
        if (f->pour) {                                    /* holding on empty space pours slowly */
            f->pour_cd -= PG_DT;
            if (f->pour_cd <= 0) { queue_drop(tx, 0, note_for_x(tx), 0.40f); f->pour_cd = 0.45f; }
        }
    } else if (t->state == PG_TOUCH_UP) {
        f->held = false;
        f->linger = t->ticks < 12 ? 7 : 0;               /* a very short tap still shoves for a moment */
        f->body->vx = f->body->vy = 0;
    } else if (!f->held && f->linger > 0) f->linger--;
    if (!f->held && f->linger <= 0) park(f->body, POOL + i);
}

/* ------------------------------------------------------------------------------------------------ the pointing-hand hint */
static void hint_update(void)
{
    bool show = idle_t >= 8.0f && fmodf(idle_t - 8.0f, 13.0f) < 8.0f;
    hint_a += ((show ? 1.0f : 0.0f) - hint_a) * (show ? 0.05f : 0.10f);
    if (hint_a < 0.03f) {
        hint_a = 0; hint_phase = 0; hint_x = -150.0f; hint_y = pile_top(-150.0f);
        hint_on = false;
        park(hint_body, POOL + PG_MAX_TOUCH);
        return;
    }
    hint_phase += PG_DT;
    float tx = 190.0f * sinf(hint_phase * 0.7f - 0.9f), ty = pile_top(tx) - 10.0f + 16.0f * sinf(hint_phase * 2.4f);
    ty = clampf(ty, FLOOR_Y + 40.0f, CEIL_Y - 120.0f);
    hint_x += (tx - hint_x) * 0.07f; hint_y += (ty - hint_y) * 0.07f;
    if (!hint_on && hint_a > 0.6f) { hint_on = true; hint_body->x = hint_x; hint_body->y = hint_y; }
    if (hint_on) {                                        /* the fingertip of the hand is a gentle ghost finger */
        float vx = (hint_x - hint_body->x) / PG_DT, vy = (hint_y - hint_body->y) / PG_DT, sp = sqrtf(vx * vx + vy * vy);
        if (sp > 380.0f) { vx *= 380.0f / sp; vy *= 380.0f / sp; }
        hint_body->vx = vx; hint_body->vy = vy;
    }
}

/* ------------------------------------------------------------------------------------------------ frame budget */
static void degrade_update(void)
{
    /* On the real Pi: if the frame climbs past ~12 ms, let the oldest balls fade out one by one. Not in headless
     * runs: there the software rasteriser makes frame_ms meaningless. */
    if (headless || (pg_ticks() % 60) != 0) return;
    float ms = pg_frame_ms();
    if (ms > 12.0f && live_cap > 36) live_cap -= 6;
    else if (ms < 8.5f && live_cap < MAX_BALLS) live_cap += 2;
    if (live_count() > live_cap) { Ball *o = oldest_live(); if (o) ball_pop(o, false); }
}

/* ------------------------------------------------------------------------------------------------ update */
static void after_step(void)
{
    for (int i = 0; i < POOL; i++) {
        Ball *b = &balls[i];
        if (b->state == B_LIVE) {
            PG_Body *bd = b->body;
            if (!isfinite(bd->x) || !isfinite(bd->y) || !isfinite(bd->vx) || !isfinite(bd->vy)) {   /* cannot happen; never trust it */
                bd->x = 0; bd->y = CEIL_Y - b->r - 8; bd->vx = bd->vy = bd->w = 0;
            }
            if (bd->x < -WALL_X - 4)       { bd->x = -WALL_X + b->r; if (bd->vx < 0) bd->vx = 0; }      /* nothing ever leaves the tub */
            else if (bd->x > WALL_X + 4)   { bd->x = WALL_X - b->r;  if (bd->vx > 0) bd->vx = 0; }
            if (bd->y < FLOOR_Y - 4)       { bd->y = FLOOR_Y + b->r; if (bd->vy < 0) bd->vy = 0; }
            else if (bd->y > CEIL_Y + 4)   { bd->y = CEIL_Y - b->r;  if (bd->vy > 0) bd->vy = 0; }
            float sp = bd->vx * bd->vx + bd->vy * bd->vy;
            if (sp > MAX_SPEED * MAX_SPEED) { float k = MAX_SPEED / sqrtf(sp); bd->vx *= k; bd->vy *= k; }
            b->x = bd->x; b->y = bd->y; b->ang = bd->angle;
            b->age += PG_DT;
            if (b->sq > 0) { b->sq_t += PG_DT; if (b->sq_t > 0.7f) b->sq = 0; }
        } else if (b->state == B_DYING) {
            b->die_t += PG_DT;
            if (b->die_t > 0.4f) b->state = B_FREE;
        }
    }
}

static void update(void)
{
    const PG_Touch *t = pg_touches();
    bool touching = false;
    for (int i = 0; i < PG_MAX_TOUCH; i++) {
        touching |= t[i].state != PG_TOUCH_NONE;
        finger_step(i, &t[i]);
    }
    idle_t = touching ? 0.0f : idle_t + PG_DT;
    hint_update();
    rain_update();
    drops_update();
    degrade_update();
    memset(snd_best, 0, sizeof snd_best);
    pg_world_step(W, PG_DT);
    after_step();
    sounds_play();
    sparks_update();
#ifdef BP_DEBUG
    if (pg_ticks() % 60 == 0) {
        float vmax = 0, vsum = 0, pen = 0; int n = 0;
        for (int i = 0; i < POOL; i++) {
            if (balls[i].state != B_LIVE) continue;
            PG_Body *a = balls[i].body; float sp = pg_body_speed(a); n++; vsum += sp; if (sp > vmax) vmax = sp;
            for (int j = i + 1; j < POOL; j++) {
                if (balls[j].state != B_LIVE) continue;
                PG_Body *b = balls[j].body; float d = hypotf(a->x - b->x, a->y - b->y), o = a->radius + b->radius - d; if (o > pen) pen = o;
                if (o > 20) pg_log("  OVERLAP %.1f: ball %d (x=%.0f y=%.0f r=%.0f v=%.0f,%.0f age=%.2f) vs %d (x=%.0f y=%.0f r=%.0f v=%.0f,%.0f age=%.2f)", o, i, a->x, a->y, a->radius, a->vx, a->vy, balls[i].age, j, b->x, b->y, b->radius, b->vx, b->vy, balls[j].age);
            }
        }
        pg_log("t=%.0f live=%d vmax=%.0f vavg=%.0f maxpen=%.1f bodies=%d", pg_time(), n, vmax, n ? vsum / n : 0, pen, pg_world_count(W));
    }
#endif
}

/* ------------------------------------------------------------------------------------------------ rendering helpers */
static PG_M4 ball_xform(float x, float y, float z, float s, float ang, float ct, float st, float yaw, float k, float nx, float ny)
{
    float ca = cosf(ang), sa = sinf(ang), cy = cosf(yaw), sy = sinf(yaw);
    /* columns of Rz(ang) * Rx(tilt) * Ry(yaw): the ball spins about its own axis, tilted, and rolls in the plane */
    float c0x = cy * ca - sy * st * sa, c0y = cy * sa + sy * st * ca, c0z = -sy * ct;
    float c1x = -ct * sa,               c1y = ct * ca,                c1z = st;
    float c2x = sy * ca + cy * st * sa, c2y = sy * sa - cy * st * ca, c2z = cy * ct;
    if (k != 0) {                                         /* squash along the impact normal, bulge across it */
        float sxx = 1 + 0.5f * k - 1.5f * k * nx * nx, sxy = -1.5f * k * nx * ny, syy = 1 + 0.5f * k - 1.5f * k * ny * ny, szz = 1 + 0.5f * k, a, b;
        a = c0x; b = c0y; c0x = sxx * a + sxy * b; c0y = sxy * a + syy * b; c0z *= szz;
        a = c1x; b = c1y; c1x = sxx * a + sxy * b; c1y = sxy * a + syy * b; c1z *= szz;
        a = c2x; b = c2y; c2x = sxx * a + sxy * b; c2y = sxy * a + syy * b; c2z *= szz;
    }
    PG_M4 m = {{ c0x * s, c0y * s, c0z * s, 0, c1x * s, c1y * s, c1z * s, 0, c2x * s, c2y * s, c2z * s, 0, x, y, z, 1 }};
    return m;
}

static void bg_quad(PG_Tex tex, PG_V3 c, float w, float h, PG_Color tint, unsigned flags)
{
    static const float sx[4] = { -1, -1, 1, 1 }, sy[4] = { 1, -1, -1, 1 }, uu[4] = { 0, 0, 1, 1 }, vv[4] = { 0, 1, 1, 0 };
    PG_Vertex v[4];
    for (int i = 0; i < 4; i++) v[i] = (PG_Vertex){ c.x + sx[i] * w * 0.5f, c.y + sy[i] * h * 0.5f, c.z, 0, 0, 1, uu[i], vv[i], tint };
    pg3d_tri(tex, &v[0], &v[1], &v[2], flags | PG3D_TWOSIDED);
    pg3d_tri(tex, &v[0], &v[2], &v[3], flags | PG3D_TWOSIDED);
}

static PG_V3 lift_to_z(PG_V3 p0, float z)                /* the point at depth z on the eye ray through p0 (z=0) */
{
    float s = 1.0f - z / cam_now.eye.z;
    return pg_v3_add(cam_now.eye, pg_v3_scale(pg_v3_sub(p0, cam_now.eye), s));
}

/* thin rope along a sagging curve, with pennants hanging from it */
static void draw_bunting(float tm, float y0, float sag, float z, int n, int phase)
{
    PG_Tex white = pg3d_white();
    const float x0 = -WALL_X + 6.0f, x1 = WALL_X - 6.0f;
    PG_Vertex a[4];
    PG_V3 prev = pg_v3(x0, y0, z);
    for (int s = 1; s <= 16; s++) {
        float f = (float)s / 16.0f, x = x0 + (x1 - x0) * f, y = y0 - sag * (1.0f - (2 * f - 1) * (2 * f - 1));
        PG_V3 cur = pg_v3(x, y, z), d = pg_v3_norm(pg_v3_sub(cur, prev)), nrm = pg_v3(-d.y * 1.6f, d.x * 1.6f, 0);
        PG_Color rope = pg_rgb(150, 112, 74);
        a[0] = (PG_Vertex){ prev.x + nrm.x, prev.y + nrm.y, z, 0, 0, 1, 0, 0, rope };
        a[1] = (PG_Vertex){ prev.x - nrm.x, prev.y - nrm.y, z, 0, 0, 1, 0, 0, rope };
        a[2] = (PG_Vertex){ cur.x - nrm.x, cur.y - nrm.y, z, 0, 0, 1, 0, 0, rope };
        a[3] = (PG_Vertex){ cur.x + nrm.x, cur.y + nrm.y, z, 0, 0, 1, 0, 0, rope };
        pg3d_tri(white, &a[0], &a[1], &a[2], PG3D_BG | PG3D_UNLIT | PG3D_TWOSIDED);
        pg3d_tri(white, &a[0], &a[2], &a[3], PG3D_BG | PG3D_UNLIT | PG3D_TWOSIDED);
        prev = cur;
    }
    for (int i = 0; i < n; i++) {
        float f = ((float)i + 0.5f) / (float)n, x = x0 + (x1 - x0) * f, y = y0 - sag * (1.0f - (2 * f - 1) * (2 * f - 1));
        PG_M4 m = pg_m4_mul(pg_m4_translate(x, y, z + 1.0f), pg_m4_rot_z(0.12f * sinf(tm * 1.5f + (float)i * 0.9f + (float)phase)));
        PG_Color c = pg_hsv((float)((i * 5 + phase) % 6) * 60.0f + 8.0f, 0.62f, 1.0f);
        pg3d_mesh(m_pennant, T.flag, &m, c, PG3D_BG | PG3D_TWOSIDED);
    }
}

static void draw_backdrop(float tm)
{
    PG_M4 id = pg_m4_identity();
    pg3d_mesh(m_paper, T.wallpaper, &id, pg_rgb(255, 255, 255), PG3D_BG);
    pg3d_mesh(m_dado, T.quilt, &id, pg_rgb(196, 178, 255), PG3D_BG);
    float breathe = 1.0f + 0.035f * sinf(tm * 1.1f), drift = 24.0f * sinf(tm * 0.11f);
    bg_quad(T.sun, pg_v3(318, 150, BACK_Z + 4), 118 * breathe, 118 * breathe, pg_rgb(255, 255, 255), PG3D_BG | PG3D_UNLIT);
    bg_quad(T.cloud, pg_v3(-300 + drift, 150, BACK_Z + 4), 170, 85, pg_rgb(255, 255, 255), PG3D_BG | PG3D_UNLIT);
    bg_quad(T.cloud, pg_v3(-95 - drift * 0.6f, 205, BACK_Z + 4), 120, 60, pg_rgb(255, 255, 255), PG3D_BG | PG3D_UNLIT);
    PG_M4 ma = pg_m4_translate(0, FLOOR_Y + 14, BACK_Z + 30);
    pg3d_mesh(m_arch, T.rainbow, &ma, pg_rgb(255, 255, 255), PG3D_BG);
    bg_quad(T.cloud, pg_v3(-338, FLOOR_Y + 70, BACK_Z + 36), 150, 75, pg_rgb(255, 255, 255), PG3D_BG | PG3D_UNLIT);
    bg_quad(T.cloud, pg_v3(338, FLOOR_Y + 70, BACK_Z + 36), 150, 75, pg_rgb(255, 255, 255), PG3D_BG | PG3D_UNLIT);
    pg3d_mesh(m_floor, T.mat, &id, pg_rgb(255, 255, 255), PG3D_BG);
    pg3d_mesh(m_side[0], T.quilt, &id, pg_rgb(128, 210, 226), PG3D_BG | PG3D_UNLIT);     /* left wall: in the shade */
    pg3d_mesh(m_side[1], T.quilt, &id, pg_rgb(168, 236, 246), PG3D_BG | PG3D_UNLIT);
    pg3d_mesh(m_ceil, T.wood, &id, pg_rgb(236, 222, 196), PG3D_BG | PG3D_UNLIT);
    draw_bunting(tm, CEIL_Y - 26, 36, BACK_Z + 80, N_FLAGS, 0);
    for (int i = 0; i < n_back; i++) {
        const BackBall *b = &back[i];
        PG_M4 m = ball_xform(b->x, b->y, b->z, b->r, b->a0 + b->spin * tm, cosf(b->tilt), sinf(b->tilt), b->a0 * 2.0f, 0, 0, 0);
        pg3d_mesh(m_sph[b->lod], T.ball[b->tex], &m, b->tint, PG3D_BG);
    }
}

static void draw_balls(void)
{
    PG_Tex shadow = pg3d_builtin(PG3D_TEX_SHADOW);
    for (int i = 0; i < POOL; i++) {
        const Ball *b = &balls[i];
        if (b->state == B_FREE) continue;
        float sc, x = b->x, y = b->y;
        if (b->state == B_DYING) {
            float k = clampf(b->die_t / 0.38f, 0, 1);
            sc = (1.0f + 0.2f * sinf(k * PG_PI)) * (1.0f - smooth01(k));
            y += 26.0f * k;
        } else sc = b->age < 0.34f ? ease_out_back(b->age / 0.34f) : 1.0f;
        if (sc < 0.02f) continue;
        float k = 0, nx = 0, ny = 0;
        if (b->state == B_LIVE && b->sq > 0) { k = b->sq * expf(-8.0f * b->sq_t) * cosf(24.0f * b->sq_t); nx = b->sqx; ny = b->sqy; }
        float yaw = b->yaw0 + 0.35f * b->ang;
        PG_M4 m = ball_xform(x, y, 0, b->r * sc, b->ang, b->ct, b->st, yaw, k, nx, ny);
        pg3d_mesh(m_sph[b->lod], T.ball[b->tex], &m, b->tint, 0);
        pg3d_shine(pg_v3(x, y, 0), b->r * sc, 0.55f);
        pg3d_sprite(shadow, pg_v3(x + 24, y - 28, BACK_Z + 40), b->r * sc * 2.6f, b->r * sc * 2.6f, 0, pg_rgba(255, 255, 255, 70), 0);
        float low = (y - b->r - FLOOR_Y) / 170.0f;
        if (b->state == B_LIVE && low < 1.0f) pg3d_blob_shadow(x + 6, FLOOR_Y + 0.8f, 8, b->r * 1.08f, 0.50f * (1.0f - clampf(low, 0, 1)));
    }
}

static void draw_lip(void)
{
    PG_M4 id = pg_m4_identity();
    pg3d_mesh(m_lip_front, T.lip, &id, pg_rgb(255, 255, 255), 0);
    pg3d_mesh(m_lip_cham, T.wood, &id, pg_rgb(255, 255, 255), 0);
    pg3d_mesh(m_lip_top, T.wood, &id, pg_rgb(255, 255, 255), 0);
}

static void draw_fx(float tm)
{
    PG_Tex star = pg3d_builtin(PG3D_TEX_STAR), glow = pg3d_builtin(PG3D_TEX_GLOW);
    for (int i = 0; i < 9; i++) {                           /* slow drifting twinkles: the room is never still */
        float ph = (float)i * 1.7f, x = 360.0f * sinf(tm * (0.06f + 0.011f * (float)i) + ph), y = 30.0f + 170.0f * sinf(tm * (0.08f + 0.007f * (float)i) + ph * 2.1f);
        float tw = 0.5f + 0.5f * sinf(tm * 0.9f + ph * 3.0f), sz = 11.0f + 7.0f * tw;
        PG_Color c = pg_hsv(40.0f + 40.0f * (float)(i % 4), 0.3f, 1.0f);
        pg3d_sprite(star, pg_v3(x, y, -30.0f + 12.0f * (float)i), sz, sz, tm * 0.3f + ph, pg_alpha(c, 0.30f + 0.30f * tw), PG3D_ADDITIVE);
        pg3d_sprite(glow, pg_v3(x, y, -30.0f + 12.0f * (float)i), sz * 2.4f, sz * 2.4f, 0, pg_alpha(c, 0.12f + 0.10f * tw), PG3D_ADDITIVE);
    }
    for (int i = 0; i < MAX_SPARKS; i++) {
        const Spark *s = &sparks[i];
        if (s->life <= 0) continue;
        float k = s->life / s->max;
        if (s->kind == SP_RING) {
            float e = 1.0f - k;
            pg3d_sprite(T.ring, s->p, s->size * (0.5f + 1.1f * e), s->size * (0.5f + 1.1f * e), 0, pg_alpha(pg_hsv(s->hue, 0.35f, 1), 0.85f * k), PG3D_ADDITIVE);
            continue;
        }
        float f = k > 0.3f ? 1.0f : k / 0.3f;
        PG_Color c = pg_hsv(s->hue, 0.55f, 1.0f);
        pg3d_sprite(star, s->p, s->size * f, s->size * f, s->spin * s->life, c, 0);
        pg3d_sprite(glow, s->p, s->size * 2.2f * f, s->size * 2.2f * f, 0, pg_alpha(c, 0.55f), PG3D_ADDITIVE);
    }
    for (int i = 0; i < PG_MAX_TOUCH; i++) {                /* glowing ring under every finger: its size is the shoving circle */
        const Finger *f = &fing[i];
        if (!f->held && f->linger <= 0) continue;
        PG_V3 p = pg_v3(f->wx, f->wy, 0);
        float pulse = 1.0f + 0.05f * sinf(tm * 8.0f + (float)i);
        PG_Color c = pg_hsv(190.0f + 40.0f * (float)i, 0.35f, 1.0f);
        pg3d_sprite(T.ring, p, FINGER_R * 2.46f * pulse, FINGER_R * 2.46f * pulse, 0, pg_alpha(c, 0.75f), PG3D_ADDITIVE);
        pg3d_sprite(glow, p, FINGER_R * 3.0f, FINGER_R * 3.0f, 0, pg_alpha(c, 0.22f), PG3D_ADDITIVE);
        for (int k = 0; k < 3; k++) {
            float a = tm * 2.3f + (float)k * 2.094f + (float)i;
            float sz = 15.0f + 5.0f * sinf(tm * 7.0f + (float)(k * 2 + i));
            pg3d_sprite(star, pg_v3(p.x + cosf(a) * FINGER_R * 0.98f, p.y + sinf(a) * FINGER_R * 0.98f, 0), sz, sz, a, pg_alpha(c, 0.9f), PG3D_ADDITIVE);
        }
    }
    if (hint_a > 0.02f) {                                  /* the friendly hand: its fingertip is the ghost finger */
        float wig = 0.10f * sinf(tm * 3.1f);
        PG_V3 tip = lift_to_z(pg_v3(hint_x, hint_y, 0), 110.0f);
        const float S = 86.0f, ox = -4.0f / 64.0f * S, oy = 27.0f / 64.0f * S;          /* sprite centre relative to the fingertip */
        float cw = cosf(wig), sw = sinf(wig);
        PG_V3 c = pg_v3(tip.x + ox * cw + oy * sw, tip.y - ox * sw + oy * cw, tip.z);
        pg3d_sprite(T.hand, c, S, S, wig, pg_alpha(pg_rgb(255, 255, 255), hint_a), 0);
        PG_V3 g = pg_v3(hint_x, hint_y, 0);
        pg3d_sprite(T.ring, g, 70.0f, 70.0f, 0, pg_alpha(pg_hsv(48, 0.4f, 1), 0.8f * hint_a * (0.7f + 0.3f * sinf(tm * 5.0f))), PG3D_ADDITIVE);
    }
}

static void render(SDL_Renderer *r)
{
    float tm = pg_time();
    PG_Camera cam = pg3d_camera_2d_fov(48);
    cam.eye.x = 42.0f * sinf(tm * 0.31f);
    cam.eye.y = 70.0f + 12.0f * sinf(tm * 0.23f + 1.0f);
    cam_now = cam;
    pg3d_begin(&cam);
    draw_backdrop(tm);
    draw_balls();
    draw_lip();
    draw_fx(tm);
    pg3d_end(r);
}

/* ------------------------------------------------------------------------------------------------ init / shutdown */
static void make_back_layer(void)
{
    static const float step = 68.0f;
    n_back = 0;
    for (float x = -WALL_X + 26.0f; x < WALL_X - 20.0f && n_back < BACK_BALLS - 3; x += step) {
        int rows = 2 + pg_rand_int(0, 1);
        float y = FLOOR_Y;
        for (int k = 0; k < rows && n_back < BACK_BALLS; k++) {
            BackBall *b = &back[n_back++];
            b->r = pg_rand_range(27, 35);
            y += b->r * (k ? 0.92f : 1.0f);
            b->x = x + pg_rand_range(-8, 8) + (k & 1 ? 20.0f : 0.0f);
            b->y = y; b->z = BACK_Z + 48.0f - (float)(k & 1) * 6.0f;
            y += b->r * 0.92f;
            float hue;
            pick_look(&b->tex, &b->tint, &hue);
            b->tint = pg_rgb((uint8_t)(b->tint.r * 0.86f), (uint8_t)(b->tint.g * 0.86f), (uint8_t)(b->tint.b * 0.88f));
            b->lod = 3;
            b->a0 = pg_rand_range(0, PG_TAU); b->tilt = pg_rand_range(-0.6f, 0.6f); b->spin = pg_rand_range(-0.12f, 0.12f);
        }
    }
}

static void build_meshes(void)
{
    m_sph[0] = pg_mesh_sphere(1, 14, 8);
    m_sph[1] = pg_mesh_sphere(1, 12, 7);
    m_sph[2] = pg_mesh_sphere(1, 10, 6);
    m_sph[3] = pg_mesh_sphere(1, 10, 5);
    m_pennant = mesh_pennant(40, 52);
    m_arch = mesh_arch(330, 28, 20, 5);

    float xl = -WALL_X - 10, bw = (WALL_X + 10) * 2.0f;
    /* back wall: padded dado below, wallpaper above (every tile maps the whole texture) */
    float dado_top = 24.0f;
    m_dado  = mesh_tiles(pg_v3(xl, FLOOR_Y, BACK_Z), pg_v3(bw / 8, 0, 0), pg_v3(0, (dado_top - FLOOR_Y) / 2, 0), 8, 2, pg_v3(0, 0, 1), true);
    m_paper = mesh_tiles(pg_v3(xl, dado_top, BACK_Z), pg_v3(bw / 8, 0, 0), pg_v3(0, (CEIL_Y + 10 - dado_top) / 2, 0), 8, 2, pg_v3(0, 0, 1), true);
    float depth = LIP_Z1 - BACK_Z, wall_h = CEIL_Y + 10 - FLOOR_Y, wall_z1 = LIP_Z1 + 160.0f;
    /* side walls (inner faces, running on past the lip so no gap shows at the screen edge), floor mat, ceiling underside */
    m_side[0] = mesh_tiles(pg_v3(-WALL_X, FLOOR_Y, BACK_Z), pg_v3(0, 0, (wall_z1 - BACK_Z) / 4), pg_v3(0, wall_h / 6, 0), 4, 6, pg_v3(1, 0, 0), true);
    m_side[1] = mesh_tiles(pg_v3(WALL_X, FLOOR_Y, BACK_Z), pg_v3(0, 0, (wall_z1 - BACK_Z) / 4), pg_v3(0, wall_h / 6, 0), 4, 6, pg_v3(-1, 0, 0), true);
    m_floor = mesh_tiles(pg_v3(-WALL_X - 90, FLOOR_Y, BACK_Z), pg_v3((WALL_X + 90) * 2 / 9, 0, 0), pg_v3(0, 0, depth / 3), 9, 3, pg_v3(0, 1, 0), false);
    m_ceil  = mesh_tiles(pg_v3(-WALL_X - 60, CEIL_Y, BACK_Z), pg_v3((WALL_X + 60) * 2 / 8, 0, 0), pg_v3(0, 0, (depth + 160.0f) / 3), 8, 3, pg_v3(0, -1, 0), false);
    pg_mesh_gradient_y(m_side[0], pg_rgb(176, 184, 200), pg_rgb(255, 255, 255));     /* darker toward the floor: a cheap contact shadow */
    pg_mesh_gradient_y(m_side[1], pg_rgb(176, 184, 200), pg_rgb(255, 255, 255));
    pg_mesh_gradient_y(m_dado, pg_rgb(186, 186, 206), pg_rgb(255, 255, 255));
    /* the front lip: painted toy blocks on the face, wood on the chamfer and top */
    float lx = -500, lw = 1000.0f / 8.0f, ch = 14.0f, zf = LIP_Z1, ztop = LIP_Z1 - ch;
    m_lip_front = mesh_tiles(pg_v3(lx, LIP_TOP - ch - 62.0f, zf), pg_v3(lw, 0, 0), pg_v3(0, 62.0f, 0), 8, 1, pg_v3(0, 0, 1), true);
    m_lip_cham  = mesh_tiles(pg_v3(lx, LIP_TOP - ch, zf), pg_v3(lw, 0, 0), pg_v3(0, ch, -ch), 8, 1, pg_v3(0, 0.7071f, 0.7071f), true);
    m_lip_top   = mesh_tiles(pg_v3(lx, LIP_TOP, LIP_Z0), pg_v3(lw, 0, 0), pg_v3(0, 0, ztop - LIP_Z0), 8, 1, pg_v3(0, 1, 0), false);
}

static int init(SDL_Renderer *r)
{
    (void)r;
    const char *drv = SDL_GetCurrentVideoDriver();
    headless = !drv || !strcmp(drv, "dummy");
    paint_textures(&T);
    build_meshes();
    pg3d_sky(pg_hex(0xBFE6FF), pg_hex(0xFFF0C8));
    pg3d_light(pg_v3(-0.34f, 0.60f, 0.72f), pg_rgb(130, 126, 114), pg_rgb(170, 176, 200), pg_rgb(114, 108, 106));

    W = pg_world_new(0, GRAVITY);
    if (!W) pg_fatal("ball-pit: out of memory (physics)");
    pg_world_set_quality(W, 3, 10);
    pg_world_set_contact_fn(W, on_contact, NULL);
    const float th = 100.0f;                                  /* thick walls: nothing tunnels through */
    PG_Body *seg[4] = {
        pg_world_add_segment(W, -WALL_X - 2 * th, FLOOR_Y - th, WALL_X + 2 * th, FLOOR_Y - th, th),
        pg_world_add_segment(W, -WALL_X - th, FLOOR_Y - 2 * th, -WALL_X - th, CEIL_Y + 2 * th, th),
        pg_world_add_segment(W, WALL_X + th, FLOOR_Y - 2 * th, WALL_X + th, CEIL_Y + 2 * th, th),
        pg_world_add_segment(W, -WALL_X - 2 * th, CEIL_Y + th, WALL_X + 2 * th, CEIL_Y + th, th) };
    for (int i = 0; i < 4; i++) { seg[i]->tag = TAG_WALL; seg[i]->restitution = 0.45f; seg[i]->friction = 0.35f; }

    /* every body that will ever exist, parked until it is needed (see the header comment) */
    for (int i = 0; i < POOL; i++) {
        PG_Body *bd = pg_world_add_circle(W, 0, 0, 30.0f, 1.0f);
        if (!bd) pg_fatal("ball-pit: physics world full");
        bd->restitution = 0.52f; bd->friction = 0.12f; bd->lin_damp = 0.25f; bd->ang_damp = 0.35f;
        bd->tag = TAG_BALL; bd->user = &balls[i];
        balls[i].body = bd; balls[i].state = B_FREE;
        park(bd, i);
    }
    for (int i = 0; i < PG_MAX_TOUCH; i++) {
        PG_Body *bd = pg_world_add_circle(W, 0, 0, FINGER_R, 0);
        if (!bd) pg_fatal("ball-pit: physics world full");
        bd->tag = TAG_FINGER; bd->restitution = 0.25f; bd->friction = 0.2f;
        fing[i].body = bd;
        park(bd, POOL + i);
    }
    hint_body = pg_world_add_circle(W, 0, 0, 32.0f, 0);
    if (!hint_body) pg_fatal("ball-pit: physics world full");
    hint_body->tag = TAG_FINGER; hint_body->restitution = 0.25f; hint_body->friction = 0.2f;
    park(hint_body, POOL + PG_MAX_TOUCH);

    make_back_layer();
    /* a first heap of balls, already settled, so the very first frame invites play */
    for (int i = 0; i < 14; i++) {
        for (int tries = 0; tries < 60; tries++) {
            float rr = pick_radius(), x = pg_rand_range(-WALL_X + rr + 20, WALL_X - rr - 20), y = pg_rand_range(FLOOR_Y + rr, FLOOR_Y + 260);
            if (!free_spot(x, y, rr)) continue;
            ball_new(x, y, 0, 0, rr, false);
            break;
        }
    }
    for (int i = 0; i < 240; i++) pg_world_step(W, PG_DT);
    for (int i = 0; i < POOL; i++) if (balls[i].state == B_LIVE) { balls[i].x = balls[i].body->x; balls[i].y = balls[i].body->y; balls[i].ang = balls[i].body->angle; }
    return 0;
}

static void shutdown(void)
{
    for (int i = 0; i < 4; i++) pg_mesh_free(m_sph[i]);
    pg_mesh_free(m_paper); pg_mesh_free(m_dado); pg_mesh_free(m_side[0]); pg_mesh_free(m_side[1]);
    pg_mesh_free(m_floor); pg_mesh_free(m_arch); pg_mesh_free(m_ceil); pg_mesh_free(m_pennant);
    pg_mesh_free(m_lip_front); pg_mesh_free(m_lip_top); pg_mesh_free(m_lip_cham);
    pg_world_free(W);
    W = NULL;
}

static const PG_Game game = { "ball-pit", "Ball Pit", init, update, render, shutdown };
int main(int argc, char **argv) { return pg_run(&game, argc, argv); }
