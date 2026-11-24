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
 * balloons have 16 px of forgiveness around them. */
#include "bp.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t inside;              /* bit per balloon slot the finger was inside last tick */
    int      trail;
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
    return 0;
}

static float finger_hue(const PG_Touch *t) { return fmodf(30.0f + 47.0f * (float)t->id, 360.0f); }

static void touch_down(const PG_Touch *t, Track *k)
{
    k->inside = 0; k->trail = 0;
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

/* BP_DEBUG_BEGIN */
void balloons_debug_dump(void);
static void debug_tick(void)
{
    const char *e = getenv("BP_DUMP");
    if (!e) return;
    unsigned want = (unsigned)atoi(e);
    if (want && pg_ticks() % want == 0) { pg_log("tick %llu balloons=%d", (unsigned long long)pg_ticks(), balloons_count()); balloons_debug_dump(); }
}
/* BP_DEBUG_END */

static void update(void)
{
    float t = pg_time();
    cam_update(t);
    const PG_Touch *T = pg_touches();
    bool any = false;
    for (int i = 0; i < PG_MAX_TOUCH; i++) {
        const PG_Touch *tc = &T[i];
        Track *k = &trk[i];
        switch (tc->state) {
        case PG_TOUCH_DOWN: any = true; touch_down(tc, k); break;
        case PG_TOUCH_HELD: any = true; touch_held(tc, k); break;
        case PG_TOUCH_UP:   any = true; break;
        default: break;
        }
    }
    if (any) { idle_s = 0.0f; ever_touched = true; } else idle_s += PG_DT;
    balloons_update(t);
    room_update(t);
    fx_update();
    snd_update();
    hint_update(idle_s, !ever_touched);
    debug_tick();
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
    for (int i = 0; i < PG_MAX_TOUCH; i++)
        if (T[i].state == PG_TOUCH_DOWN || T[i].state == PG_TOUCH_HELD) fx_touch_glow(&T[i]);
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
