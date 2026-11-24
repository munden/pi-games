/* hint.c - Draw & Roll: the hint hand.
 *
 * At the start, and whenever nobody has touched the screen for ~10 s, a white-gloved hand fades in, taps down and
 * draws a real rainbow ramp with the same pen code the fingers use, then waves goodbye. Touching the screen at any
 * moment sends it away. It never chases or startles: it only appears in a quiet scene.
 */
#include "hint.h"
#include "fx.h"
#include "strokes.h"

#include <string.h>

#define MAXP 72

enum { H_IDLE = 0, H_APPEAR, H_DRAW, H_LINGER, H_LEAVE };

static PG_Tex t_hand;
static struct {
    int   state;
    float t, idle, fade, pos, total, hx, hy;
    bool  first;
    int   n, which;
    float px[MAXP], py[MAXP], cum[MAXP];
} H;

/* control points of the demo lines (world px); each run mirrors/jitters them a little */
#define NCTRL 8
static const struct { int n; float p[NCTRL][2]; bool mirror; } PATHS[4] = {
    { 5, { { 70, 70 }, { 150, 62 }, { 235, 38 }, { 300, 8 }, { 345, -20 } }, false },                 /* catches balls on the right */
    { 5, { { -170, 138 }, { -255, 116 }, { -330, 76 }, { -392, 22 }, { -410, -10 } }, true },        /* a swoosh in the sky */
    { 5, { { -380, 24 }, { -220, -2 }, { -340, -34 }, { -200, -58 }, { -170, -70 } }, true },         /* a zigzag chute */
    { 6, { { 170, -14 }, { 235, 8 }, { 295, -22 }, { 350, 2 }, { 400, -28 }, { 420, -40 } }, true },  /* a wavy ramp */
};

void hint_init(void)
{
    PG_Image *im = pg_img_new(64, 64, 0x00FFFFFFu);
    const uint32_t outline = PG_RGB24(0x5A6FC4), shade = PG_RGB24(0xBCCBF2), white = PG_RGB24(0xFFFFFF), cuff = PG_RGB24(0xFF4F72), cuff2 = PG_RGB24(0xB8284A);
    for (int pass = 0; pass < 3; pass++) {
        float o = pass == 1 ? 1.6f : 0.0f, g = pass == 0 ? 2.2f : 0.0f;     /* pass 0: soft outline, 1: shade, 2: white */
        uint32_t c = pass == 0 ? outline : pass == 1 ? shade : white;
        pg_img_disc(im, 30 + o, 42 + o, 15.5f + g, c);
        pg_img_line(im, 28 + o, 36 + o, 22 + o, 10 + o, 10.5f + 2 * g, c);  /* the pointing finger */
        pg_img_line(im, 18 + o, 45 + o, 9 + o, 37 + o, 8.5f + 2 * g, c);    /* thumb */
        pg_img_disc(im, 41 + o, 35 + o, 6.8f + g, c);
        pg_img_disc(im, 45 + o, 44 + o, 6.8f + g, c);
        pg_img_disc(im, 41 + o, 52 + o, 6.2f + g, c);
    }
    pg_img_line(im, 30, 57, 35, 66, 26.0f, outline);
    pg_img_line(im, 30, 57, 35, 66, 23.0f, cuff2);
    pg_img_line(im, 29, 56, 33, 65, 20.0f, cuff);
    pg_img_line(im, 20, 40, 20, 24, 1.4f, PG_ARGB(120, 150, 170, 225));     /* knuckle crease */
    t_hand = tex_finish(im, true);
    memset(&H, 0, sizeof H);
    H.first = true;
}

void hint_shutdown(void) {}

void hint_skip_to(float idle_seconds) { if (H.state == H_IDLE) H.idle = idle_seconds; }

/* Catmull-Rom through the control points, jittered and maybe mirrored, then arc lengths */
static void build_path(int which)
{
    int cn = PATHS[which].n;
    float mirror = (PATHS[which].mirror && (pg_rand() & 1u)) ? -1.0f : 1.0f;
    float ox = pg_rand_range(-34.0f, 34.0f), oy = pg_rand_range(-16.0f, 16.0f), sc = pg_rand_range(0.92f, 1.08f);
    float cx[NCTRL], cy[NCTRL];
    for (int i = 0; i < cn; i++) {
        cx[i] = PATHS[which].p[i][0] * sc * mirror + ox * mirror;
        cy[i] = PATHS[which].p[i][1] * sc + oy;
    }
    H.n = 0;
    for (int i = 0; i + 1 < cn; i++) {
        int i0 = i > 0 ? i - 1 : i, i3 = i + 2 < cn ? i + 2 : i + 1;
        for (int k = 0; k < 8 && H.n < MAXP; k++) {
            float t = (float)k / 8.0f, t2 = t * t, t3 = t2 * t;
            H.px[H.n] = 0.5f * ((2 * cx[i]) + (-cx[i0] + cx[i + 1]) * t + (2 * cx[i0] - 5 * cx[i] + 4 * cx[i + 1] - cx[i3]) * t2 + (-cx[i0] + 3 * cx[i] - 3 * cx[i + 1] + cx[i3]) * t3);
            H.py[H.n] = 0.5f * ((2 * cy[i]) + (-cy[i0] + cy[i + 1]) * t + (2 * cy[i0] - 5 * cy[i] + 4 * cy[i + 1] - cy[i3]) * t2 + (-cy[i0] + 3 * cy[i] - 3 * cy[i + 1] + cy[i3]) * t3);
            H.n++;
        }
    }
    if (H.n < MAXP) { H.px[H.n] = cx[cn - 1]; H.py[H.n] = cy[cn - 1]; H.n++; }
    H.cum[0] = 0.0f;
    for (int i = 1; i < H.n; i++) H.cum[i] = H.cum[i - 1] + hypotf(H.px[i] - H.px[i - 1], H.py[i] - H.py[i - 1]);
    H.total = H.cum[H.n - 1];
}

static void point_at(float d, float *x, float *y)
{
    d = clampf(d, 0.0f, H.total);
    int i = 1;
    while (i < H.n - 1 && H.cum[i] < d) i++;
    float seg = H.cum[i] - H.cum[i - 1], t = seg > 1e-3f ? (d - H.cum[i - 1]) / seg : 0.0f;
    *x = lerpf(H.px[i - 1], H.px[i], t); *y = lerpf(H.py[i - 1], H.py[i], t);
}

static void start_hint(void)
{
    static int next_path;
    H.which = next_path++ & 3;
    build_path(H.which);
    H.state = H_APPEAR; H.t = 0.0f; H.pos = 0.0f; H.fade = 0.0f;
    H.hx = H.px[0]; H.hy = H.py[0];
}

static void abort_hint(void)
{
    if (strokes_pen_active(PEN_HINT)) strokes_pen_up(PEN_HINT);
    if (H.state != H_LEAVE) { H.state = H_LEAVE; H.t = 0.0f; }
}

void hint_update(bool any_down, bool any_active)
{
    if (any_down) { H.idle = 0.0f; H.first = false; if (H.state != H_IDLE) abort_hint(); }   /* a child who already plays needs no first demo */
    else if (!any_active) H.idle += PG_DT;
    H.t += PG_DT;
    switch (H.state) {
    case H_IDLE: {
        float need = H.first ? 1.3f : (strokes_count() == 0 ? 3.6f : 10.0f);
        if (H.idle >= need && !any_active) start_hint();
        break; }
    case H_APPEAR:
        H.fade = smooth01(H.t / 0.4f);
        if (H.t >= 0.5f) {                                          /* the tap that starts the line */
            strokes_pen_down(PEN_HINT, H.px[0], H.py[0], PEN_RAINBOW);
            strokes_pen_set_ttl(PEN_HINT, 8.0f);                  /* the demo ramp melts away by itself, so repeats never pile up */
            fx_burst(H.px[0], H.py[0], 50.0f, 6, 130.0f, 50.0f, 60.0f, 18.0f);
            H.state = H_DRAW; H.t = 0.0f;
        }
        break;
    case H_DRAW: {
        float speed = 120.0f + 300.0f * smooth01(H.t / 0.35f);
        float left = H.total - H.pos;
        if (left < 80.0f) speed *= 0.35f + 0.65f * (left / 80.0f);
        H.pos += speed * PG_DT;
        point_at(H.pos, &H.hx, &H.hy);
        strokes_pen_move(PEN_HINT, H.hx, H.hy);
        if (H.pos >= H.total - 1.0f) { strokes_pen_up(PEN_HINT); H.state = H_LINGER; H.t = 0.0f; }
        break; }
    case H_LINGER:
        if (H.t >= 0.4f) { H.state = H_LEAVE; H.t = 0.0f; }
        break;
    case H_LEAVE:
        H.fade = fmaxf(0.0f, H.fade - PG_DT * 2.4f);
        H.hy += 26.0f * PG_DT; H.hx += 14.0f * PG_DT;
        if (H.fade <= 0.0f) { H.state = H_IDLE; H.idle = 0.0f; H.first = false; }
        break;
    }
}

void hint_render(void)
{
    if (H.state == H_IDLE || H.fade <= 0.01f) return;
    float t = pg_time();
    float bounce = H.state == H_APPEAR ? 1.0f + 0.25f * (1.0f - smooth01(H.t / 0.5f)) : 1.0f;
    float S = 108.0f * bounce;
    float roll = -0.12f + sinf(t * 4.0f) * 0.04f;
    /* the fingertip is at texture (0.34, 0.08): place the sprite so that point sits exactly on the pen, even though the
     * sprite floats in front of the plane (the perspective magnifies it, so the offset is divided by depth_k) */
    float k = depth_k(70.0f), lift = H.state == H_APPEAR ? 14.0f * (1.0f - smooth01(H.t / 0.5f)) : 0.0f;
    PG_V3 c = at_depth(H.hx + 0.16f * S / k, H.hy - 0.42f * S / k + lift, 70.0f);
    pg3d_sprite(t_hand, c, S, S, roll, pg_rgba(255, 255, 255, (uint8_t)(255.0f * clampf(H.fade, 0.0f, 1.0f))), 0);
}
