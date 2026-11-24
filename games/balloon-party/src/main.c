/* Balloon Party - float, pop and bat balloons in a sunny party room.
 *
 * How a child plays (no instructions, no text):
 *   touch a balloon          -> it pops: confetti, a big sparkle and a bell note (each colour is a note)
 *   swipe through balloons   -> they are batted away with a squashy boing
 *   touch empty air          -> a puff of sparkles that nudges the balloons nearby
 *   sometimes a popped balloon releases a spinning star / heart / music note; touch it for a twinkle
 * Nothing can be lost or wrong; popped balloons are replaced from below within a second.
 * When nobody touches the screen a cartoon hand points at a balloon and taps it (without popping).
 *
 * Touch rules (see touch_* below): a finger that lands on a balloon pops it on that very tick (a landing
 * finger is by definition a slow touch). A finger that is moving slower than BAT_SPEED (250 px/s) pops each
 * new balloon it runs into; a faster one bats everything its path crosses. Nothing needs a precise position:
 * balloons have 16 px of forgiveness around them.
 *
 * A contact that has not moved for STILL_TICKS (a resting palm, or a beam stuck on the IR frame) stops counting
 * as activity and its glow fades, so the hint hand still shows up and nothing sticks on the screen. */
#include "bp.h"

#include <string.h>

#define STILL_TICKS 480                      /* 8 s without moving */
#define STILL_FADE  60

_Static_assert(MAX_BALLOONS <= 32, "the touch tracker keeps one bit per balloon slot in a uint32_t");

typedef struct {
    uint32_t inside;                         /* bit per balloon slot the finger was inside last tick */
    int      trail;                          /* counts ticks of fast movement to space the sparkle trail */
    int      still;                          /* ticks the contact has hardly moved */
} Track;

static Track trk[PG_MAX_TOUCH];
static float idle_s;
static bool  ever_touched;

static int init(SDL_Renderer *r)
{
    (void)r;
    cam_update(0.0f);
    room_init();
    balloons_init();
    fx_init();
    memset(trk, 0, sizeof trk);
    idle_s = 0.0f; ever_touched = false;
    return 0;
}

static float finger_hue(const PG_Touch *t) { return fmodf(30.0f + 47.0f * (float)t->id, 360.0f); }

static void touch_down(const PG_Touch *t, Track *k)
{
    k->inside = 0; k->trail = 0; k->still = 0;
    for (int j = 0; j < balloon_slots(); j++) if (balloon_contains(j, t->x, t->y, HIT_PAD)) k->inside |= 1u << j;
    int pz = fx_prize_hit(t->x, t->y);
    if (pz >= 0) { fx_prize_pop(pz); return; }
    int bi = balloon_hit(t->x, t->y, HIT_PAD);
    if (bi >= 0) { balloon_pop(bi); return; }                 /* visible and audible on the very next frame */
    /* empty air: a puff of sparkles, a soft plink, nearby balloons wobble away */
    float wx, wy;
    cam_unproject(t->x, t->y, 60.0f, &wx, &wy);
    fx_burst(pg_v3(wx, wy, 60.0f), finger_hue(t), 6, 190.0f);
    fx_ripple(pg_v3(wx, wy, 60.0f), 120.0f, pg_hsv(finger_hue(t), 0.55f, 1.0f));
    balloons_puff(t->x, t->y);
    snd_tap();
}

static void touch_held(const PG_Touch *t, Track *k)
{
    /* a blend of the instantaneous and the smoothed finger velocity: responsive but not twitchy */
    float vx = 0.55f * (t->x - t->px) / PG_DT + 0.45f * t->vx, vy = 0.55f * (t->y - t->py) / PG_DT + 0.45f * t->vy;
    float spd = sqrtf(t->vx * t->vx + t->vy * t->vy);
    bool fast = spd > BAT_SPEED;

    k->still = hypotf(t->x - t->px, t->y - t->py) < 0.4f ? k->still + 1 : 0;
    if (k->still > STILL_TICKS) return;                       /* a resting palm or a stuck beam: ignore */

    uint32_t now_in = 0;
    for (int j = 0; j < balloon_slots(); j++) {
        bool in = balloon_contains(j, t->x, t->y, HIT_PAD);
        if (in) now_in |= 1u << j;
        if (fast) { if (balloon_swept(j, t->px, t->py, t->x, t->y, 22.0f)) balloon_bat(j, vx, vy); }
        else if (spd > 40.0f && in && !(k->inside & (1u << j))) balloon_pop(j);
    }
    k->inside = now_in;

    if (spd > 140.0f && (++k->trail & 1) == 0) {               /* a sparkle trail behind a moving finger */
        float wx, wy;
        cam_unproject(t->x, t->y, 100.0f, &wx, &wy);
        fx_trail(pg_v3(wx, wy, 100.0f), finger_hue(t));
    }
}

#ifdef BP_DEBUG
#include <stdlib.h>
void balloons_debug_dump(void);
static void debug_tick(void)
{
    const char *e = getenv("BP_DUMP");           /* dev only: comma separated ticks, e.g. BP_DUMP=149,160,175 */
    if (!e) return;
    unsigned long long now = (unsigned long long)pg_ticks();
    for (const char *q = e; *q; ) {
        char *end;
        unsigned long long v = strtoull(q, &end, 10);
        if (end == q) break;
        if (v == now) { pg_log("tick %llu balloons=%d", now, balloons_count()); balloons_debug_dump(); }
        q = *end == ',' ? end + 1 : end;
    }
}
#endif

static void update(void)
{
    float t = pg_time();
    cam_update(t);
    const PG_Touch *T = pg_touches();
    bool active = false;
    for (int i = 0; i < PG_MAX_TOUCH; i++) {
        const PG_Touch *tc = &T[i];
        Track *k = &trk[i];
        switch (tc->state) {
        case PG_TOUCH_DOWN: touch_down(tc, k); active = true; break;
        case PG_TOUCH_HELD: touch_held(tc, k); if (k->still <= STILL_TICKS) active = true; break;
        case PG_TOUCH_UP:   active = true; break;
        default: break;
        }
    }
    if (active) { idle_s = 0.0f; ever_touched = true; } else idle_s += PG_DT;
    balloons_update(t);
    room_update(t);
    fx_update();
    snd_update();
    hint_update(idle_s, !ever_touched);
#ifdef BP_DEBUG
    debug_tick();
#endif
}

static void render(SDL_Renderer *r)
{
    pg3d_begin(&cam_get()->pg);
    room_draw_wall();
    balloons_draw_shadows();
    room_draw_floor();
    balloons_draw();
    fx_draw();
    hint_draw();
    const PG_Touch *T = pg_touches();
    for (int i = 0; i < PG_MAX_TOUCH; i++) {
        if (T[i].state != PG_TOUCH_DOWN && T[i].state != PG_TOUCH_HELD) continue;
        if (trk[i].still <= STILL_TICKS + STILL_FADE)
            fx_touch_glow(&T[i], 1.0f - smooth01((float)(trk[i].still - STILL_TICKS) / (float)STILL_FADE));
    }
    room_draw_ambient();
    pg3d_end(r);
}

static void shutdown(void)
{
    fx_free();
    balloons_free();
    room_free();
}

static const PG_Game game = { "balloon-party", "Balloon Party", init, update, render, shutdown };
int main(int argc, char **argv) { return pg_run(&game, argc, argv); }
