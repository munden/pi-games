/* Jelly Blobs - six squishy candy jellies with big faces in a toy-box playroom.
 *
 * Touch a blob to grab it where you touched: the skin stretches toward your finger, wobbles when you let go and
 * boings.  A quick poke squishes it (and makes it giggle).  Fling one and it bounces around and piles up against the
 * walls.  Two fingers on one blob stretch it like dough.  Touching the mat makes nearby blobs hop.
 * Nothing can be lost or wrong; blobs breathe, blink, look at your fingers and hop by themselves, and a friendly
 * glove shows how to play after a couple of idle seconds.
 *
 * soft.c = soft-body physics, scene.c = room + blob drawing, art.c = painted textures. */
#include "pg3d.h"

#include "art.h"
#include "scene.h"
#include "soft.h"

#define NBLOB      6
#define HINT_ID    16                 /* the glove uses the 17th finger slot of the physics */
#define MAX_PARTS  72
#define MAX_NOTES  24

/* ----------------------------------------------------------------------- state */
static Art       art;
static SoftWorld W;
static BlobFx    fx[SB_MAX];

typedef struct { bool active; int blob; float x, y, vx, vy; } Finger;
static Finger fing[SB_FINGERS];

typedef struct { float x, y, z, vx, vy, life, max, size, rot, vrot; PG_Color col; int kind; } Part;
enum { P_STAR, P_HEART, P_PUFF, P_SPARK };
static Part  parts[MAX_PARTS];
static int   part_next;

typedef struct { int tick; PG_Wave wave; float f0, f1, ms, vol; } Note;
static Note  notes[MAX_NOTES];

typedef struct {
    int   state;                      /* 0 away, 1 approach, 2 press, 3 drag, 4 let go, 5 leave */
    float t, alpha, x, y, sx, sy, tx, ty, dx, dy;
    int   blob, next_blob;
} Hint;
static Hint  hint;
static float idle_t;                  /* seconds since a real finger last touched anything */
static float chime_cool;
static uint32_t life_ticks;

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float ease(float t) { t = clampf(t, 0, 1); return t * t * (3.0f - 2.0f * t); }

/* ------------------------------------------------------------------------ effects */
static void spawn(int kind, float x, float y, float z, float vx, float vy, float life, float size, PG_Color col)
{
    Part *p = &parts[part_next];
    part_next = (part_next + 1) % MAX_PARTS;
    p->kind = kind; p->x = x; p->y = y; p->z = z; p->vx = vx; p->vy = vy;
    p->life = p->max = life; p->size = size; p->rot = pg_rand_range(0, PG_TAU); p->vrot = pg_rand_range(-4, 4); p->col = col;
}

static void burst(int kind, float x, float y, int n, float speed, float size, PG_Color col)
{
    for (int i = 0; i < n; i++) {
        float a = pg_rand_range(0, PG_TAU), s = pg_rand_range(0.4f, 1.0f) * speed;
        spawn(kind, x, y, 70.0f, cosf(a) * s, sinf(a) * s + speed * 0.35f, pg_rand_range(0.7f, 1.2f), size * pg_rand_range(0.8f, 1.2f), col);
    }
}

static float pitch_for(int i) { return 1.5f - (W.b[i].radius - 60.0f) / 35.0f * 0.7f; }

static void note_push(int delay_ticks, PG_Wave w, float f0, float f1, float ms, float vol)
{
    for (int i = 0; i < MAX_NOTES; i++)
        if (notes[i].tick < 0) { notes[i].tick = (int)W.ticks + delay_ticks; notes[i].wave = w; notes[i].f0 = f0; notes[i].f1 = f1; notes[i].ms = ms; notes[i].vol = vol; return; }
}

/* "hee-hee-hee": three or four quick little pentatonic blips */
static void giggle(int bi)
{
    BlobFx *f = &fx[bi];
    if (f->giggle_t > 0.0f) return;
    f->giggle_t = 0.75f;
    int base = 5 + (int)(1.4f * (pitch_for(bi) - 0.8f) * 3.0f);
    static const int seq[4] = { 0, 2, 1, 3 };
    int n = 3 + (int)(pg_rand() & 1u);
    for (int k = 0; k < n; k++) {
        float f0 = pg_note_hz(base + seq[k]);
        note_push(4 + k * 5, PG_WAVE_SINE, f0, f0 * 1.18f, 70.0f, 0.26f);
        note_push(4 + k * 5, PG_WAVE_TRI, f0 * 2.0f, f0 * 2.3f, 50.0f, 0.03f);
    }
}

static void notes_update(void)
{
    for (int i = 0; i < MAX_NOTES; i++)
        if (notes[i].tick >= 0 && (int)W.ticks >= notes[i].tick) {
            pg_tone(notes[i].wave, notes[i].f0, notes[i].f1, notes[i].ms, notes[i].vol);
            notes[i].tick = -1;
        }
}

/* the glove is drawn lifted toward the camera: put its world point on the ray through the z=0 point */
static void lift(float t, float x, float y, float z, PG_V3 *out)
{
    PG_Camera c;
    scene_camera(t, &c);
    float k = (c.eye.z - z) / c.eye.z;
    *out = pg_v3(c.eye.x + (x - c.eye.x) * k, c.eye.y + (y - c.eye.y) * k, z);
}

/* ----------------------------------------------------------------------- fingers */
static int  snd_this_tick;
static uint32_t last_squish_tick;

/* Every touch must be heard: the SDK's squish is rate limited, so extra simultaneous touches get a soft pentatonic blip. */
static void touch_sound(int bi, float vol)
{
    if (snd_this_tick >= 4) return;
    if (snd_this_tick == 0 && W.ticks - last_squish_tick >= 4) {
        pg_sfx_squish(vol, pitch_for(bi));
        last_squish_tick = W.ticks;
    } else {
        float f = pg_note_hz(4 + (bi * 2 + snd_this_tick * 3) % 8);
        pg_tone(PG_WAVE_SINE, f, f * 1.25f, 85.0f, vol * 0.4f);
    }
    snd_this_tick++;
}

static void touch_blob(int id, int bi, float wx, float wy, float poke)
{
    Finger *f = &fing[id];
    soft_grab(&W, id, bi, wx, wy);
    f->blob = bi;
    soft_poke(&W, bi, wx, wy, poke);
    touch_sound(bi, id == HINT_ID ? 0.28f : 0.55f);
    fx[bi].poke_t = 0.45f;
    if (id != HINT_ID) giggle(bi);
    burst(P_HEART, wx, wy, 1, 150.0f, 26.0f, pg_rgb(255, 255, 255));
    burst(P_STAR, wx, wy, 3, 220.0f, 22.0f, art_candy(fx[bi].style));
}

static void finger_down(int id, float wx, float wy)
{
    Finger *f = &fing[id];
    f->active = true; f->x = wx; f->y = wy; f->vx = f->vy = 0; f->blob = -1;
    int bi = soft_pick(&W, wx, wy, 46.0f);
    if (bi >= 0) touch_blob(id, bi, wx, wy, 250.0f);
    else {
        /* the mat or the wall: a sparkle, a bell, and anything standing nearby hops */
        if (chime_cool <= 0) {
            pg_sfx_chime(pg_rand_int(1, 9), 0.42f);
            chime_cool = 0.09f;
        } else if (snd_this_tick < 4) {
            float hz = pg_note_hz(pg_rand_int(3, 9));
            pg_tone(PG_WAVE_SINE, hz, hz, 140.0f, 0.2f);
            snd_this_tick++;
        }
        burst(P_STAR, wx, wy, 5, 240.0f, 24.0f, pg_hsv(pg_rand_range(0, 360), 0.45f, 1.0f));
        soft_slap(&W, wx, wy, 230.0f, 330.0f);
    }
}

static void finger_move(int id, float wx, float wy)
{
    Finger *f = &fing[id];
    float vx = (wx - f->x) * 60.0f, vy = (wy - f->y) * 60.0f;
    f->vx = 0.6f * f->vx + 0.4f * vx; f->vy = 0.6f * f->vy + 0.4f * vy;
    f->x = wx; f->y = wy;
    if (f->blob < 0 && id != HINT_ID && f->vx * f->vx + f->vy * f->vy > 140.0f * 140.0f) {
        /* a finger that swipes onto a blob picks it up */
        int bi = soft_pick(&W, wx, wy, 0.0f);
        if (bi >= 0) touch_blob(id, bi, wx, wy, 150.0f);
    }
    if (f->blob >= 0) {
        soft_move(&W, id, wx, wy, f->vx, f->vy);
        if ((life_ticks & 3u) == 0 && f->vx * f->vx + f->vy * f->vy > 300.0f * 300.0f)
            spawn(P_STAR, wx, wy, 70.0f, pg_rand_range(-40, 40), pg_rand_range(-10, 60), 0.6f, 18.0f, art_candy(fx[f->blob].style));
    }
}

static void finger_up(int id)
{
    Finger *f = &fing[id];
    if (f->blob >= 0) soft_release(&W, id);
    f->active = false; f->blob = -1;
}

/* -------------------------------------------------------------------- blob behaviour */
static void blob_think(int i)
{
    BlobFx *f = &fx[i];
    SoftBlob *b = &W.b[i];
    const float dt = PG_DT;

    f->phase += dt * (1.1f + 0.07f * (float)i);
    b->breath = 0.010f * sinf(f->phase * 2.3f);
    b->ell_bias = 0.016f * sinf(f->phase * 2.3f + 1.2f);
    if (f->giggle_t > 0) f->giggle_t -= dt;
    if (f->poke_t > 0) f->poke_t -= dt;
    if (f->land_t > 0) f->land_t -= dt;
    if (f->mouth_t > 0) f->mouth_t -= dt;
    if (f->snd_cool > 0) f->snd_cool -= dt;

    /* blink, sometimes twice */
    if (f->blink_p > 0) {
        f->blink_p += dt / 0.17f;
        if (f->blink_p >= 1.0f) { f->blink_p = 0; f->blink_t = (pg_randf() < 0.22f) ? 0.12f : pg_rand_range(2.2f, 5.6f); }
    } else if ((f->blink_t -= dt) <= 0) f->blink_p = 0.001f;

    /* gaze: the nearest finger (or the glove), else wander and sometimes glance at a friend */
    float tx = f->tx, ty = f->ty, best = 640.0f * 640.0f;
    bool looking = false;
    for (int k = 0; k < SB_FINGERS; k++) {
        if (!fing[k].active) continue;
        float dx = fing[k].x - b->cx, dy = fing[k].y - b->cy, d2 = dx * dx + dy * dy;
        if (fing[k].blob == i) d2 *= 0.25f;
        if (d2 < best) { best = d2; tx = fing[k].x; ty = fing[k].y; looking = true; }
    }
    if (!looking) {
        if ((f->wander_t -= dt) <= 0) {
            f->wander_t = pg_rand_range(1.2f, 3.6f);
            if (pg_randf() < 0.45f) {
                int o = pg_rand_int(0, W.n - 1);
                if (o == i) o = (o + 1) % W.n;
                f->tx = W.b[o].cx; f->ty = W.b[o].cy + 20.0f;
            } else { f->tx = pg_rand_range(-420, 420); f->ty = pg_rand_range(-180, 200); }
        }
        tx = f->tx; ty = f->ty;
    }
    float gdx = (tx - b->cx) / 140.0f, gdy = (ty - b->cy) / 140.0f, gl = sqrtf(gdx * gdx + gdy * gdy);
    if (gl > 1.0f) { gdx /= gl; gdy /= gl; }
    float rate = looking ? 0.22f : 0.08f;
    f->gx += (gdx - f->gx) * rate; f->gy += (gdy - f->gy) * rate;

    /* the face jiggles a little behind the body */
    float jtx = clampf(-b->vcx * 0.016f - (b->x[SB_C] - b->cx) * 0.25f, -9.0f, 9.0f), jty = clampf(-b->vcy * 0.016f, -9.0f, 9.0f);
    f->jvx += ((jtx - f->jx) * 160.0f - f->jvx * 7.0f) * dt; f->jvy += ((jty - f->jy) * 160.0f - f->jvy * 7.0f) * dt;
    f->jx += f->jvx * dt; f->jy += f->jvy * dt;

    /* the leaf / antenna on top swings behind the motion */
    float at = clampf(b->vcx * 0.0011f + b->tilt * 0.6f, -0.55f, 0.55f);     /* counter-clockwise: moving right, the tip lags to the left */
    f->acc_v += ((at - f->acc_a) * 110.0f - f->acc_v * 4.5f) * dt; f->acc_a += f->acc_v * dt;

    /* expression follows the deformation */
    int want = FACE_SMILE;
    bool airborne = !b->grounded && b->lowest > 14.0f;
    if (f->poke_t > 0) want = FACE_OH;
    else if (f->giggle_t > 0) want = FACE_GRIN;
    else if (b->held > 0 && b->stretch > 0.22f) want = FACE_OH;
    else if (airborne && b->vcy < -520.0f) want = FACE_OH;
    else if (airborne && b->vcy > 80.0f) want = FACE_GRIN;
    else if (f->land_t > 0) want = FACE_GRIN;
    else if (b->stretch > 0.42f) want = FACE_OH;
    else if (airborne) want = f->mouth == FACE_OH ? FACE_OH : FACE_GRIN;
    if (want != f->mouth && f->mouth_t <= 0) { f->mouth = want; f->mouth_t = 0.22f; }
    f->wide += ((f->mouth == FACE_OH ? 1.0f : 0.0f) - f->wide) * 0.2f;

    /* idle hop: crouch, then jump a little (never while someone holds it) */
    if (f->crouch_t > 0) {
        b->ell_extra = -0.24f;
        if ((f->crouch_t -= dt) <= 0) {
            float side = pg_rand_range(-55.0f, 55.0f);
            if (b->cx > SB_WALL_X - 140) side = -fabsf(side);
            if (b->cx < -SB_WALL_X + 140) side = fabsf(side);
            soft_hop(&W, i, pg_rand_range(380.0f, 470.0f), side);
            f->hop_t = pg_rand_range(6.0f, 14.0f);
        }
    } else if ((f->hop_t -= dt) <= 0) {
        if (b->grounded && b->held == 0 && life_ticks > 150) f->crouch_t = 0.15f;
        else f->hop_t = 0.4f;
    }
}

/* ------------------------------------------------------------------------- hint glove */
static const float HAND_SIZE = 96.0f;

static void hint_stop_finger(void)
{
    if (fing[HINT_ID].active) finger_up(HINT_ID);
}

static void hint_update(bool real_touch)
{
    Hint *h = &hint;
    const float dt = PG_DT;
    if (real_touch) {
        if (h->state != 0 && h->state != 5) { hint_stop_finger(); h->state = 5; h->t = 0.0f; }
        if (h->state == 5) { h->alpha -= dt * 5.0f; if (h->alpha <= 0) { h->alpha = 0; h->state = 0; } }
        return;
    }
    if (h->state == 0) {
        if (idle_t > 2.2f) {
            int bi = h->next_blob % W.n;
            for (int k = 0; k < W.n; k++) {                            /* prefer a blob that is standing around */
                int c = (bi + k) % W.n;
                if (W.b[c].held == 0 && W.b[c].lowest < 20.0f) { bi = c; break; }
            }
            h->next_blob = bi + 1; h->blob = bi;
            const SoftBlob *b = &W.b[bi];
            /* the glove takes hold of the blob's upper inner side, lifts it and lets go: there is always room above */
            h->dx = b->cx > 60.0f ? -1.0f : 1.0f;
            h->tx = b->cx + h->dx * b->radius * 0.45f; h->ty = b->cy + b->radius * 0.55f;
            h->sx = clampf(h->tx + h->dx * 200.0f, -430.0f, 430.0f); h->sy = SB_FLOOR_Y - 150.0f;
            h->x = h->sx; h->y = h->sy;
            h->state = 1; h->t = 0; h->alpha = 0;
        }
        return;
    }
    h->t += dt;
    h->alpha = clampf(h->alpha + dt * 4.0f, 0.0f, 1.0f);
    const SoftBlob *b = &W.b[h->blob];
    switch (h->state) {
    case 1: {                                                          /* glide in */
        float e = ease(h->t / 1.15f);
        h->tx = b->cx + h->dx * b->radius * 0.45f; h->ty = b->cy + b->radius * 0.55f;
        h->x = h->sx + (h->tx - h->sx) * e; h->y = h->sy + (h->ty - h->sy) * e + sinf(e * PG_PI) * 30.0f;
        if (h->t >= 1.15f) { h->state = 2; h->t = 0; h->x = h->tx; h->y = h->ty; finger_down(HINT_ID, h->x, h->y); h->sx = h->x; h->sy = h->y; }
        break;
    }
    case 2:                                                            /* press */
        finger_move(HINT_ID, h->x, h->y);
        if (h->t >= 0.40f) { h->state = 3; h->t = 0; }
        break;
    case 3: {                                                          /* pull the jelly a little way and back */
        float e = ease(h->t / 0.95f);
        float arc = sinf(e * PG_PI);
        h->x = h->sx + h->dx * 38.0f * arc;
        h->y = h->sy + (95.0f + b->radius * 0.6f) * arc;
        finger_move(HINT_ID, h->x, h->y);
        if (h->t >= 0.95f) { hint_stop_finger(); h->state = 4; h->t = 0; }
        break;
    }
    case 4:
        if (h->t >= 0.35f) { h->state = 5; h->t = 0; }
        break;
    default: {                                                         /* wave goodbye, slide away */
        h->y -= dt * 260.0f; h->x += h->dx * dt * 60.0f;
        h->alpha -= dt * 2.4f;
        if (h->alpha <= 0 || h->t > 1.2f) { h->alpha = 0; h->state = 0; idle_t = -3.0f; }  /* come back after ~5 s of nothing */
        break;
    }
    }
}

/* ---------------------------------------------------------------------------- init */
static int init(SDL_Renderer *r)
{
    (void)r;
    art_init(&art);
    scene_init(&art);
    static const float R[NBLOB] = { 82, 68, 92, 62, 76, 70 };
    static const int   style[NBLOB] = { 0, 1, 2, 3, 4, 5 };      /* strawberry, lime, grape, lemon, sky, orange */
    static const float X[NBLOB] = { -352, -214, -78, 64, 204, 342 };
    static const float Y[NBLOB] = { 80, 150, 40, 170, 100, 190 };
    float y[NBLOB];
    for (int i = 0; i < NBLOB; i++) y[i] = SB_FLOOR_Y + Y[i];
    soft_init(&W, NBLOB, R, X, y);
    for (int i = 0; i < NBLOB; i++) {
        BlobFx *f = &fx[i];
        memset(f, 0, sizeof *f);
        f->style = style[i];
        f->blink_t = pg_rand_range(1.0f, 4.0f);
        f->hop_t = pg_rand_range(4.0f, 9.0f) + 0.9f * (float)i;
        f->wander_t = pg_rand_range(0.2f, 2.0f);
        f->tx = pg_rand_range(-300, 300); f->ty = pg_rand_range(-100, 150);
        f->mouth = FACE_SMILE;
        f->phase = pg_rand_range(0, PG_TAU);
    }
    for (int i = 0; i < MAX_NOTES; i++) notes[i].tick = -1;
    for (int i = 0; i < MAX_PARTS; i++) parts[i].life = 0;
    hint.next_blob = 2;
    idle_t = 0;
    return 0;
}

/* --------------------------------------------------------------------------- update */
static void handle_events(void)
{
    const SoftEvent *ev;
    int n = soft_events(&W, &ev);
    for (int k = 0; k < n; k++) {
        const SoftEvent *e = &ev[k];
        switch (e->kind) {
        case SE_LAND: {
            BlobFx *f = &fx[e->a];
            if (f->snd_cool <= 0) {
                pg_sfx_boing(clampf(0.05f + e->speed / 2200.0f, 0.08f, 0.45f), pitch_for(e->a) * pg_rand_range(0.94f, 1.06f));
                f->snd_cool = 0.16f;
            }
            f->land_t = 0.45f;
            int np = 2 + (int)(e->speed / 280.0f);
            for (int i = 0; i < np; i++) {
                float dir = (i & 1) ? 1.0f : -1.0f;
                spawn(P_PUFF, e->x + dir * W.b[e->a].radius * pg_rand_range(0.5f, 0.9f), SB_FLOOR_Y + 6.0f, 40.0f, dir * pg_rand_range(40, 140), pg_rand_range(10, 50), 0.55f, 34.0f, pg_rgba(255, 255, 255, 150));
            }
            break;
        }
        case SE_WALL:
            if (fx[e->a].snd_cool <= 0) {
                pg_sfx_boing(clampf(0.08f + e->speed / 2600.0f, 0.10f, 0.32f), pitch_for(e->a) * 1.12f);
                fx[e->a].snd_cool = 0.16f;
            }
            burst(P_STAR, e->x * 0.97f, e->y, 2, 160.0f, 18.0f, art_candy(fx[e->a].style));
            break;
        case SE_BUMP: {
            int step = 2 + ((e->a * 2 + e->b * 3) % 8);
            pg_sfx_chime(step, clampf(0.26f + e->speed / 3000.0f, 0.26f, 0.42f));
            burst(P_STAR, e->x, e->y, 4, 200.0f, 22.0f, pg_hsv(pg_rand_range(0, 360), 0.4f, 1.0f));
            fx[e->a].land_t = fmaxf(fx[e->a].land_t, 0.25f); fx[e->b].land_t = fmaxf(fx[e->b].land_t, 0.25f);
            /* the two look at each other for a moment */
            fx[e->a].tx = W.b[e->b].cx; fx[e->a].ty = W.b[e->b].cy + 20.0f; fx[e->a].wander_t = 1.4f;
            fx[e->b].tx = W.b[e->a].cx; fx[e->b].ty = W.b[e->a].cy + 20.0f; fx[e->b].wander_t = 1.4f;
            break;
        }
        case SE_RELEASE: {
            const SoftBlob *b = &W.b[e->a];
            if (b->stretch > 0.30f) {
                pg_sfx_boing(0.38f, pitch_for(e->a) * 1.2f);
                burst(P_STAR, b->cx, b->cy, 3, 220.0f, 22.0f, art_candy(fx[e->a].style));
            }
            if (e->speed > 650.0f) pg_sfx_whoosh(0.14f);
            break;
        }
        case SE_HEAL:
            pg_sfx_sparkle(0.25f);
            burst(P_SPARK, e->x, e->y, 6, 160.0f, 40.0f, pg_rgb(255, 255, 255));
            break;
        default: break;
        }
    }
}

static void update(void)
{
    const PG_Touch *t = pg_touches();
    const float now = pg_time();
    bool real = false;
    life_ticks++;
    snd_this_tick = 0;
    if (chime_cool > 0) chime_cool -= PG_DT;

    for (int i = 0; i < PG_MAX_TOUCH; i++) {
        if (t[i].state == PG_TOUCH_NONE) { if (fing[i].active) finger_up(i); continue; }
        float wx, wy;
        if (!scene_unproject(now, t[i].x, t[i].y, &wx, &wy)) continue;
        real = true;
        if (t[i].state == PG_TOUCH_DOWN) finger_down(i, wx, wy);
        else if (fing[i].active) {
            finger_move(i, wx, wy);
            if (t[i].state == PG_TOUCH_UP) finger_up(i);
        } else if (t[i].state == PG_TOUCH_HELD) finger_down(i, wx, wy);
    }
    if (real) idle_t = 0; else idle_t += PG_DT;
    hint_update(real);

    for (int i = 0; i < W.n; i++) blob_think(i);
    soft_step(&W);
    handle_events();
    notes_update();

    for (int i = 0; i < MAX_PARTS; i++) {
        Part *p = &parts[i];
        if (p->life <= 0) continue;
        p->life -= PG_DT;
        if (p->kind == P_PUFF) { p->vx *= 0.96f; p->vy = p->vy * 0.96f + 10.0f * PG_DT; p->size += 36.0f * PG_DT; }   /* dust drifts up and spreads */
        else p->vy -= 420.0f * PG_DT;
        p->x += p->vx * PG_DT; p->y += p->vy * PG_DT; p->rot += p->vrot * PG_DT;
    }
}

/* -------------------------------------------------------------------------- render */
static void draw_effects(float tm)
{
    PG_Tex star = pg3d_builtin(PG3D_TEX_STAR), glow = pg3d_builtin(PG3D_TEX_GLOW);
    PG_Tex heart = art_cell(art.fx, FX_HEART, 4, 2), puff = art_cell(art.fx, FX_PUFF, 4, 2), spark = art_cell(art.fx, FX_SPARKLE, 4, 2);
    for (int i = 0; i < MAX_PARTS; i++) {
        const Part *p = &parts[i];
        if (p->life <= 0) continue;
        float k = clampf(p->life / 0.35f, 0.0f, 1.0f), ph = 1.0f - p->life / p->max;
        PG_V3 pos = pg_v3(p->x, p->y, p->z);
        switch (p->kind) {
        case P_STAR:
            pg3d_sprite(star, pos, p->size * k, p->size * k, p->rot, p->col, 0);
            pg3d_sprite(glow, pos, p->size * 2.0f * k, p->size * 2.0f * k, 0, pg_alpha(p->col, 0.45f), PG3D_ADDITIVE);
            break;
        case P_HEART:
            pg3d_sprite(heart, pg_v3(p->x, p->y + ph * 30.0f, p->z), p->size * (0.6f + 0.6f * k), p->size * (0.6f + 0.6f * k), 0.2f * sinf(tm * 8.0f + p->rot), pg_alpha(p->col, k), 0);
            break;
        case P_PUFF:
            pg3d_sprite(puff, pos, p->size, p->size * 0.8f, 0, pg_alpha(p->col, k * (float)p->col.a / 255.0f), 0);
            break;
        default:
            pg3d_sprite(spark, pos, p->size * k, p->size * k, p->rot, p->col, PG3D_ADDITIVE);
            break;
        }
    }
    /* a soft glow under every finger so the child sees it was felt */
    PG_Tex g = pg3d_builtin(PG3D_TEX_GLOW);
    for (int i = 0; i < SB_FINGERS; i++) {
        if (!fing[i].active) continue;
        PG_V3 p;
        lift(tm, fing[i].x, fing[i].y, 90.0f, &p);
        PG_Color c = fing[i].blob >= 0 ? art_candy(fx[fing[i].blob].style) : pg_hsv(40.0f + 60.0f * (float)i, 0.35f, 1.0f);
        c = pg_color_lerp(c, pg_rgb(255, 255, 255), 0.45f);
        pg3d_sprite(g, p, 92, 92, 0, pg_alpha(c, 0.42f), PG3D_ADDITIVE);
    }
}

static void draw_hand(float tm)
{
    const Hint *h = &hint;
    if (h->state == 0 || h->alpha <= 0.01f) return;
    const float roll = -0.38f, S = HAND_SIZE;
    /* fingertip is at texel (26, 6) of 64 in the glove sprite: find the sprite centre that puts it on (x,y) */
    float ox = (26.0f - 32.0f) / 64.0f * S, oy = (6.0f - 32.0f) / 64.0f * S;           /* screen-space offset centre->tip (y down) */
    float cs = cosf(roll), sn = sinf(roll);
    float tipx = ox * cs - oy * sn, tipy = ox * sn + oy * cs;
    float bob = sinf(tm * 7.0f) * 3.0f * (h->state == 1 ? 1.0f : 0.0f);
    PG_V3 p;
    lift(tm, h->x - tipx * 0.84f, h->y + tipy * 0.84f + bob, 120.0f, &p);
    PG_Tex shadow = art_cell(art.fx, FX_SHADOW, 4, 2);
    pg3d_sprite(shadow, pg_v3(p.x + 14, p.y - 20, p.z - 6), S * 0.9f, S * 0.7f, 0, pg_rgba(80, 40, 110, (uint8_t)(70.0f * h->alpha)), 0);
    pg3d_sprite(art.hand, p, S, S, roll, pg_rgba(255, 255, 255, (uint8_t)(255.0f * h->alpha)), 0);
}

static void render(SDL_Renderer *r)
{
    float tm = pg_time();
    PG_Camera cam;
    scene_camera(tm, &cam);
    pg3d_begin(&cam);
    scene_draw_room(tm);
    for (int i = 0; i < W.n; i++) scene_draw_shadow(&W.b[i]);
    for (int i = 0; i < W.n; i++) scene_draw_blob(&W.b[i], &fx[i], tm);
    draw_effects(tm);
    draw_hand(tm);
    pg3d_end(r);
}

static void shutdown(void)
{
    scene_shutdown();
}

static const PG_Game game = { "jelly-blobs", "Jelly Blobs", init, update, render, shutdown };
int main(int argc, char **argv) { return pg_run(&game, argc, argv); }
