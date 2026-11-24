/* Draw & Roll - children draw ramps with their fingers and shiny balls roll along them into a smiling bucket.
 *
 * Up to ten fingers draw at once (each stroke its own bright colour); every piece of a stroke is a solid physics segment
 * and a glossy 3D tube. A friendly cloud drops balls by itself and when tapped; the bucket eats them with a chime;
 * a big sponge washes all the ramps away. See README.md.
 *
 * Module map: strokes.c (ramps) balls.c (balls) toys.c (cloud, bucket, sponge) scene.c (diorama + static physics)
 *             fx.c (sparkles) hint.c (pointing hand) util.c (meshes, textures, sound helpers)
 */
#include "balls.h"
#include "fx.h"
#include "hint.h"
#include "scene.h"
#include "strokes.h"
#include "toys.h"
#include "util.h"

#include <math.h>
#include <string.h>

enum { TK_NONE = 0, TK_DRAW, TK_SPONGE };

typedef struct { uint8_t kind; bool inside; } TouchInfo;

static PG_World *world;
static TouchInfo ti[PG_MAX_TOUCH];
static float     load_ms, degrade;           /* my own CPU cost per frame (update + submit + pg3d), smoothed */
static double    upd_ms;

static double now_ms(void) { return (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency(); }

static PG_Camera make_camera(float t)
{
    PG_Camera c = pg3d_camera_2d_fov(44.0f);          /* z=0 plane stays ~1:1 with canvas pixels */
    c.eye.x = sinf(t * 0.27f) * 26.0f;                /* slow sway: parallax between the backdrop, the toys and the walls */
    c.eye.y = 26.0f + sinf(t * 0.19f + 1.0f) * 9.0f;
    return c;
}

/* ----------------------------------------------------------------- physics events -> sound and juice */
static void on_contact(PG_Body *a, PG_Body *b, float impact, float x, float y, void *ud)
{
    (void)ud;
    PG_Body *ball = a->tag == TAG_BALL ? a : (b->tag == TAG_BALL ? b : NULL);
    if (!ball) return;
    PG_Body *other = ball == a ? b : a;
    balls_contact(ball, impact, x, y);
    switch (other->tag) {
    case TAG_STROKE: {
        bool fresh = strokes_contact(other);
        if (fresh && impact > 120.0f) {
            int step = 2 + (int)clampf((y + 260.0f) / 520.0f * 8.0f, 0.0f, 8.0f);        /* higher on the screen = higher note */
            float vol = 0.12f + 0.34f * clampf((impact - 120.0f) / 900.0f, 0.0f, 1.0f);
            snd_note(step, vol, 430.0f);
            if (impact > 220.0f) fx_burst(x, y, 30.0f, impact > 520.0f ? 2 : 1, 110.0f, 20.0f + (float)step * 30.0f, 30.0f, 13.0f);
        }
        break; }
    case TAG_BALL:
        balls_contact(other, impact, x, y);
        if (impact > 160.0f && snd_ready(3, 6)) pg_sfx_tap(0.14f);
        break;
    case TAG_WALL:
        scene_wall_hit(scene_wall_side(other), impact);
        if (impact > 150.0f && snd_ready(4, 8)) pg_sfx_boing(0.2f, 1.15f);
        break;
    case TAG_HILL:
        if (impact > 230.0f && snd_ready(5, 8)) pg_sfx_thud(0.16f);
        break;
    default: break;
    }
}

/* ------------------------------------------------------------------------- touch */
static void do_wipe(void)
{
    strokes_wipe();
    toys_sponge_wipe();
    hint_skip_to(0.0f);
}

static void touch_down(int i, const PG_Touch *p, const PG_V3 *w)
{
    TouchInfo *k = &ti[i];
    memset(k, 0, sizeof *k);
    if (toys_hit_sponge(p->x, p->y)) {                   /* the sponge: reacts now, washes when the finger lets go */
        k->kind = TK_SPONGE; k->inside = true;
        toys_sponge_press(true);
        return;
    }
    if (!w) return;
    k->kind = TK_DRAW;
    int c = strokes_next_color();
    strokes_pen_down(i, w->x, w->y, c);
    fx_burst(w->x, w->y, 44.0f, 7, 190.0f, 20.0f + (float)c * 36.0f, 35.0f, 20.0f);   /* instant sparkle: a tap is always answered */
    snd_note(2 + c, 0.27f, 520.0f);
    toys_look_at(w->x, w->y);
    if (toys_hit_dispenser(p->x, p->y)) balls_drop();
}

static void touch_held(int i, const PG_Touch *p, const PG_V3 *w)
{
    TouchInfo *k = &ti[i];
    if (k->kind == TK_DRAW && w) {
        strokes_pen_move(i, w->x, w->y);
        toys_look_at(w->x, w->y);
    } else if (k->kind == TK_SPONGE) {
        bool in = toys_hit_sponge(p->x, p->y);
        if (in != k->inside) { k->inside = in; if (!in) toys_sponge_press(false); }
    }
}

static void touch_up(int i, const PG_Touch *p, const PG_V3 *w)
{
    TouchInfo *k = &ti[i];
    if (k->kind == TK_DRAW) {
        if (w) strokes_pen_move(i, w->x, w->y);
        strokes_pen_up(i);
    } else if (k->kind == TK_SPONGE) {
        toys_sponge_press(false);
        if (toys_hit_sponge(p->x, p->y) && p->ticks <= 150) do_wipe();     /* a tap, not a resting palm */
    }
    k->kind = TK_NONE;
}

static void handle_touches(void)
{
    const PG_Touch *t = pg_touches();
    bool any_down = false, any_active = false;
    for (int i = 0; i < PG_MAX_TOUCH; i++) {
        const PG_Touch *p = &t[i];
        if (p->state == PG_TOUCH_NONE) { ti[i].kind = TK_NONE; continue; }
        any_active = true;
        PG_V3 w;
        bool ok = pg3d_plane_z(p->x, p->y, 0.0f, &w) && isfinite(w.x) && isfinite(w.y);
        if (p->state == PG_TOUCH_DOWN)      { any_down = true; touch_down(i, p, ok ? &w : NULL); }
        else if (p->state == PG_TOUCH_HELD) touch_held(i, p, ok ? &w : NULL);
        else                                touch_up(i, p, ok ? &w : NULL);
    }
    hint_update(any_down, any_active);
}

/* ----------------------------------------------------------------------- the game */
static int init(SDL_Renderer *r)
{
    (void)r;
    world = pg_world_new(0.0f, -900.0f);
    if (!world) pg_fatal("draw-roll: out of memory (physics)");
    pg_world_set_quality(world, 3, 12);                  /* 3 substeps: fast balls cannot tunnel through thin ramps */
    pg_world_set_contact_fn(world, on_contact, NULL);

    pg3d_sky(pg_hex(0x6EC0FF), pg_hex(0xFFEBC8));
    pg3d_light(pg_v3(-0.42f, 0.78f, 0.62f), pg_rgb(214, 204, 186), pg_rgb(176, 186, 214), pg_rgb(146, 136, 128));

    fx_init();
    scene_init();
    scene_physics(world);
    strokes_init(world);
    balls_init(world);
    toys_init();
    hint_init();

    /* the automatic first ramp: a gentle S under the cloud that ends above the bucket's right lip */
    static const float first[] = { -210, 88, -130, 74, -50, 48, 30, 8, 90, -38, 132, -80 };
    strokes_add_line(first, 6, 5);

    PG_Camera cam = make_camera(0.0f);                   /* so pg3d_plane_z / pg3d_project are valid during the first update */
    view_set(&cam);
    pg3d_begin(&cam);
    return 0;
}

static void update(void)
{
    double t0 = now_ms();
    handle_touches();
    scene_update();
    toys_update();
    strokes_update(degrade);
    balls_update(degrade);
    pg_world_step(world, PG_DT);
    fx_update(degrade);
    upd_ms = now_ms() - t0;
#ifdef DR_STATS
    if (pg_ticks() % 60 == 0) pg_log("stats t=%d pieces=%d total=%d spheres=%d balls=%d fx=%d tris=%d load=%.2f upd=%.2f bodies=%d", (int)(pg_ticks() / 60), strokes_count(), strokes_total(), strokes_spheres(), balls_count(), fx_count(), pg3d_last_tris(), load_ms, upd_ms, pg_world_count(world));
#endif
}

static void render(SDL_Renderer *r)
{
    double t0 = now_ms();
    PG_Camera cam = make_camera(pg_time());
    view_set(&cam);
    pg3d_begin(&cam);
    scene_render_back();
    strokes_render_shadows();
    balls_render_shadows();
    toys_render_shadows();
    scene_render_front();
    toys_render();
    strokes_render();
    balls_render();
    fx_render();
    hint_render();
    double pre3d = pg3d_cpu_ms(), t1 = now_ms();
    pg3d_end(r);
    /* my CPU cost, excluding the (software) rasteriser: on the Pi ~12 ms here means "stop piling things on" */
    float cur = (float)(upd_ms + (t1 - t0 - pre3d) + pg3d_cpu_ms());
    load_ms = load_ms == 0.0f ? cur : load_ms * 0.93f + cur * 0.07f;
    degrade = clampf((load_ms - 9.0f) / 6.0f, 0.0f, 1.0f);
}

static void shutdown(void)
{
    hint_shutdown();
    toys_shutdown();
    balls_shutdown();
    strokes_shutdown();
    scene_shutdown();
    fx_shutdown();
    pg_world_free(world);
    world = NULL;
}

static const PG_Game game = { "draw-roll", "Draw & Roll", init, update, render, shutdown };
int main(int argc, char **argv) { return pg_run(&game, argc, argv); }
