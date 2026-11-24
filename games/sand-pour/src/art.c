/* art.c - procedural textures and meshes for the Sand Pour diorama (a sunny beach toy-box). */
#include "art.h"

#include <math.h>
#include <stdlib.h>

#define RGB(c)   PG_RGB24(c)
#define ARGB(a, c) PG_ARGB(a, ((c) >> 16) & 255, ((c) >> 8) & 255, (c) & 255)

static PG_Tex finish(PG_Image *im)
{
    pg_img_ps1(im);
    PG_Tex t = pg3d_texture(im);
    pg_img_free(im);
    return t;
}

/* ------------------------------------------------------------------ textures */
static PG_Tex paint_wood(int w, int h, int vertical, uint32_t lightc, uint32_t darkc)
{
    PG_Image *im = pg_img_new(w, h, RGB(lightc));
    if (!vertical) pg_img_gradient_v(im, 0, 0, w, h, RGB(lightc), RGB(darkc));
    else for (int x = 0; x < w; x++) pg_img_rect(im, x, 0, 1, h, pg_argb_lerp(RGB(lightc), RGB(darkc), (float)x / (float)(w - 1)));
    int n = w * h / 30;
    for (int i = 0; i < n; i++) {
        int x = pg_rand_int(0, w - 1), y = pg_rand_int(0, h - 1), len = pg_rand_int(10, (vertical ? h : w) / 3);
        uint32_t c = (i & 1) ? ARGB(52, 0x7A3E12) : ARGB(46, 0xFFE0A8);
        if (vertical) pg_img_rect(im, x, y, 1, len, c); else pg_img_rect(im, x, y, len, 1, c);
    }
    int knots = (vertical ? h : w) / 40;
    for (int i = 0; i < knots; i++) {
        float cx = vertical ? (float)pg_rand_int(3, w - 4) : (float)pg_rand_int(6, w - 7), cy = vertical ? (float)pg_rand_int(6, h - 7) : (float)pg_rand_int(3, h - 4);
        pg_img_disc(im, cx, cy, vertical ? 2.6f : 2.8f, ARGB(110, 0x8A4A1C));
        pg_img_ring(im, cx, cy, 3.6f, 1.0f, ARGB(70, 0x8A4A1C));
    }
    if (!vertical) { pg_img_rect(im, 0, 0, w, 1, ARGB(140, 0xFFEDC0)); pg_img_rect(im, 0, h - 1, w, 1, ARGB(120, 0x6A3410)); }
    else           { pg_img_rect(im, 0, 0, 1, h, ARGB(140, 0xFFEDC0)); pg_img_rect(im, w - 1, 0, 1, h, ARGB(120, 0x6A3410)); }
    pg_img_noise(im, 6, 11u + (uint32_t)w);
    return finish(im);
}

static PG_Tex paint_post(void)                       /* white toy block, tinted per corner */
{
    PG_Image *im = pg_img_new(32, 32, RGB(0xF4F1EA));
    pg_img_gradient_v(im, 0, 0, 32, 32, RGB(0xFFFFFF), RGB(0xD9D3C8));
    pg_img_frame(im, 0, 0, 32, 32, 2, ARGB(120, 0xFFFFFF));
    pg_img_disc(im, 16, 16, 9.0f, ARGB(255, 0xCFC8BC));
    pg_img_disc(im, 15.2f, 15.2f, 7.4f, ARGB(255, 0xFFFFFF));
    pg_img_noise(im, 4, 5);
    return finish(im);
}

static PG_Tex paint_wall(void)                       /* the back of the sand window: calm pastel sky wall */
{
    PG_Image *im = pg_img_new(128, 64, 0);
    pg_img_gradient_v(im, 0, 0, 128, 64, RGB(0x9FD9FF), RGB(0xFFEFD2));
    for (int j = 0; j < 5; j++) for (int i = 0; i < 7; i++)
        pg_img_disc(im, 8.0f + (float)i * 20.0f + (float)(j & 1) * 10.0f, 6.0f + (float)j * 14.0f, 2.8f, ARGB(80, 0xFFFFFF));
    for (int i = 0; i < 5; i++) pg_img_star(im, 14.0f + (float)i * 26.0f, 14.0f + (float)(i * 7 % 22), 4.6f, 2.0f, 5, 0.3f * (float)i, ARGB(150, 0xFFFFFF));
    for (int x = 0; x < 128; x++) {
        int y = 54 + (int)(2.4f * sinf((float)x * 0.16f));
        pg_img_rect(im, x, y, 1, 64 - y, ARGB(120, 0xFFF4D6));
    }
    pg_img_noise(im, 4, 21);
    return finish(im);
}

static PG_Tex paint_ground(void)                     /* beach sand: ripples, speckles, shells */
{
    PG_Image *im = pg_img_new(128, 128, RGB(0xF3DDA0));
    pg_img_gradient_v(im, 0, 0, 128, 128, RGB(0xF6E3AE), RGB(0xEBCF8C));
    for (int i = 0; i < 1100; i++) {
        int x = pg_rand_int(0, 127), y = pg_rand_int(0, 127);
        pg_img_rect(im, x, y, 1 + (i & 1), 1, (i % 3) ? ARGB(120, 0xC9A460) : ARGB(140, 0xFFF3CC));
    }
    for (int r = 0; r < 6; r++)
        for (int x = 0; x < 128; x++) {
            int y = 10 + r * 21 + (int)(3.2f * sinf((float)x * 0.11f + (float)r * 1.7f));
            pg_img_rect(im, x, y, 1, 1, ARGB(80, 0xCDA866));
            pg_img_rect(im, x, y + 1, 1, 1, ARGB(70, 0xFFF6D8));
        }
    static const uint32_t shell[4] = { 0xFFD9E6, 0xFFFFFF, 0xF7B6A3, 0xB9D6EC };
    for (int i = 0; i < 9; i++) pg_img_disc(im, (float)pg_rand_int(4, 123), (float)pg_rand_int(4, 123), 1.6f + (float)(i % 3) * 0.7f, ARGB(235, shell[i & 3]));
    pg_img_noise(im, 5, 31);
    return finish(im);
}

static PG_Tex paint_sea(void)
{
    PG_Image *im = pg_img_new(128, 64, 0);
    pg_img_gradient_v(im, 0, 0, 128, 24, RGB(0xBDF3F2), RGB(0x55CBE3));
    pg_img_gradient_v(im, 0, 24, 128, 40, RGB(0x55CBE3), RGB(0x2F86D8));
    for (int j = 6; j < 50; j += 4)
        for (int k = 0; k < 5; k++) pg_img_rect(im, pg_rand_int(0, 120), j + pg_rand_int(0, 2), pg_rand_int(5, 14), 1, ARGB(150, 0xFFFFFF));
    for (int x = 0; x < 128; x++) {
        int y = 49 + (int)(1.8f * sinf((float)x * 0.33f));
        pg_img_rect(im, x, y, 1, 3, ARGB(220, 0xFFFFFF));
    }
    pg_img_noise(im, 4, 41);
    return finish(im);
}

static PG_Tex paint_rays(void)
{
    PG_Image *im = pg_img_new(128, 128, 0);
    pg_img_star(im, 64, 64, 63, 38, 12, 0, ARGB(235, 0xFFB52E));
    return finish(im);
}

/* mode 0 smile, 1 blink, 2 laugh */
static PG_Tex paint_sun(int mode)
{
    PG_Image *im = pg_img_new(128, 128, 0);
    pg_img_disc(im, 64, 64, 60, ARGB(255, 0xFF9B24));
    pg_img_disc(im, 64, 64, 55, ARGB(255, 0xFFC633));
    pg_img_disc(im, 60, 59, 44, ARGB(255, 0xFFDC4D));
    pg_img_disc(im, 55, 53, 28, ARGB(120, 0xFFF1A0));
    pg_img_disc(im, 34, 76, 9, ARGB(130, 0xFF6F7E));
    pg_img_disc(im, 94, 76, 9, ARGB(130, 0xFF6F7E));
    if (mode == 0) {
        for (int e = 0; e < 2; e++) {
            float ex = e ? 82.0f : 46.0f;
            pg_img_disc(im, ex, 52, 5.6f, ARGB(255, 0x6A3410)); pg_img_disc(im, ex, 58, 5.6f, ARGB(255, 0x6A3410));
            pg_img_disc(im, ex + 1.8f, 50, 2.0f, ARGB(255, 0xFFFFFF));
        }
    } else {
        for (int e = 0; e < 2; e++) {
            float ex = e ? 82.0f : 46.0f;
            pg_img_line(im, ex - 7, 58, ex, 51, 3.6f, ARGB(255, 0x6A3410));
            pg_img_line(im, ex, 51, ex + 7, 58, 3.6f, ARGB(255, 0x6A3410));
        }
    }
    if (mode < 2) {
        float px = 40, py = 76;
        for (int i = 1; i <= 10; i++) {
            float t = (float)i / 10.0f, x = 40.0f + 48.0f * t, y = 76.0f + 15.0f * (1.0f - (2 * t - 1) * (2 * t - 1));
            pg_img_line(im, px, py, x, y, 4.6f, ARGB(255, 0x8A3A14));
            px = x; py = y;
        }
    } else {
        for (int dy = 0; dy < 21; dy++) {
            float hw = sqrtf(22.0f * 22.0f - (float)dy * (float)dy * 1.2f);
            if (hw > 0) pg_img_rect(im, (int)(64 - hw), 74 + dy, (int)(2 * hw), 1, ARGB(255, 0x9A2230));
        }
        pg_img_disc(im, 64, 94, 9.0f, ARGB(255, 0xFF7C8C));
        pg_img_rect(im, 44, 72, 40, 4, ARGB(255, 0xFFFFFF));
    }
    return finish(im);
}

static PG_Tex paint_cloud(void)
{
    PG_Image *im = pg_img_new(64, 32, 0);
    static const float d[6][3] = { { 16, 20, 9 }, { 29, 14, 12 }, { 44, 18, 11 }, { 53, 23, 7 }, { 24, 24, 8 }, { 40, 24, 9 } };
    for (int i = 0; i < 6; i++) pg_img_disc(im, d[i][0], d[i][1] + 2.2f, d[i][2], ARGB(255, 0xCFE3FA));
    for (int i = 0; i < 6; i++) pg_img_disc(im, d[i][0], d[i][1], d[i][2], ARGB(255, 0xFFFFFF));
    return finish(im);
}
static PG_Tex paint_trunk(void)
{
    PG_Image *im = pg_img_new(32, 64, RGB(0xA66E3E));
    pg_img_rect(im, 5, 0, 4, 64, ARGB(60, 0xFFD9A0));
    pg_img_rect(im, 22, 0, 4, 64, ARGB(60, 0x5A2E10));
    for (int y = 0; y < 64; y += 7) { pg_img_rect(im, 0, y, 32, 2, ARGB(150, 0x6E4220)); pg_img_rect(im, 0, y + 2, 32, 1, ARGB(70, 0xFFD9A0)); }
    pg_img_noise(im, 9, 51);
    return finish(im);
}

static PG_Tex paint_leaf(void)                       /* a palm frond: spine plus leaflets, cut out by alpha */
{
    PG_Image *im = pg_img_new(64, 64, 0);
    for (int i = 0; i < 11; i++) {
        float x = 4.0f + (float)i * 5.2f, len = 26.0f - (float)i * 1.9f;
        uint32_t c = pg_argb_lerp(RGB(0x2E9E48), RGB(0x8CE07A), (float)i / 10.0f);
        pg_img_line(im, x, 32, x + 10.0f, 32 - len, 4.8f, c);
        pg_img_line(im, x, 32, x + 10.0f, 32 + len, 4.8f, pg_argb_lerp(c, RGB(0x1F7A3C), 0.35f));
    }
    pg_img_line(im, 2, 32, 62, 32, 3.4f, ARGB(255, 0x2A7F3B));
    pg_img_line(im, 2, 31, 60, 31, 1.2f, ARGB(255, 0xA7F08E));
    return finish(im);
}

static PG_Tex paint_button(void)                     /* big red cap: an open hatch with rainbow sand falling through */
{
    PG_Image *im = pg_img_new(128, 128, 0);
    pg_img_disc(im, 64, 64, 63, ARGB(255, 0xA62A2A));
    pg_img_disc(im, 64, 64, 59, ARGB(255, 0xE8433B));
    pg_img_gradient_radial(im, 52, 48, 58, RGB(0xFF7D6E), RGB(0xE23A34));
    for (int y = 0; y < 128; y++)                    /* keep the cap round: clear whatever the gradient spilled outside it */
        for (int x = 0; x < 128; x++) {
            float dx = (float)x + 0.5f - 64.0f, dy = (float)y + 0.5f - 64.0f;
            if (dx * dx + dy * dy > 62.5f * 62.5f) im->px[y * 128 + x] = 0;
        }
    pg_img_ring(im, 64, 64, 56, 3, ARGB(120, 0xFFFFFF));
    /* the hatch box (white) with an open bottom */
    pg_img_rect(im, 30, 28, 68, 7, ARGB(255, 0xFFFFFF));
    pg_img_rect(im, 30, 28, 7, 38, ARGB(255, 0xFFFFFF));
    pg_img_rect(im, 91, 28, 7, 38, ARGB(255, 0xFFFFFF));
    pg_img_rect(im, 30, 59, 22, 7, ARGB(255, 0xFFFFFF));
    pg_img_rect(im, 76, 59, 22, 7, ARGB(255, 0xFFFFFF));
    /* rainbow grains falling through the gap, then a big chunky arrow */
    static const uint32_t rb[6] = { 0xFFD23A, 0xFF9A2E, 0xF26EB8, 0x7CD6FF, 0x9A62E8, 0x58C95A };
    for (int i = 0; i < 6; i++) pg_img_disc(im, 56.0f + (float)(i * 5 % 17), 40.0f + (float)i * 6.5f, 3.6f, ARGB(255, rb[i]));
    pg_img_rect(im, 57, 86, 14, 12, ARGB(255, 0xFFFFFF));
    for (int dy = 0; dy < 13; dy++) pg_img_rect(im, 64 - 17 + dy * 17 / 13, 98 + dy, 34 - dy * 34 / 13, 1, ARGB(255, 0xFFFFFF));
    return finish(im);
}

static PG_Tex paint_stripes_tex(int w, int h, int sw, uint32_t a, uint32_t b)
{
    PG_Image *im = pg_img_new(w, h, RGB(a));
    pg_img_stripes(im, sw, 0, RGB(a), RGB(b));
    pg_img_rect(im, 0, 0, w, 1, ARGB(120, 0xFFFFFF));
    pg_img_noise(im, 5, 71);
    return finish(im);
}

static PG_Tex paint_crate(void)
{
    PG_Image *im = pg_img_new(64, 64, RGB(0x3E86DB));
    pg_img_gradient_v(im, 0, 0, 64, 64, RGB(0x59A0EE), RGB(0x2F6FC4));
    for (int y = 16; y < 64; y += 16) { pg_img_rect(im, 0, y - 1, 64, 1, ARGB(110, 0xA8D0FF)); pg_img_rect(im, 0, y, 64, 2, ARGB(130, 0x1C3F86)); }
    pg_img_frame(im, 0, 0, 64, 64, 3, ARGB(150, 0xBFE0FF));
    for (int i = 0; i < 4; i++) pg_img_disc(im, (i & 1) ? 56.0f : 8.0f, (i & 2) ? 56.0f : 8.0f, 2.8f, ARGB(255, 0xFFFFFF));
    pg_img_noise(im, 6, 81);
    return finish(im);
}

static PG_Tex paint_hand(void)                       /* a friendly pointing mitten with a blue sleeve, finger tip at the bottom */
{
    PG_Image *im = pg_img_new(64, 64, 0);
    const uint32_t edge = 0xD4825A, skin = 0xFFD3A6;
    for (int pass = 0; pass < 2; pass++) {
        uint32_t c = pass ? ARGB(255, skin) : ARGB(255, edge);
        float k = pass ? 0.0f : 3.2f;
        pg_img_line(im, 32, 56, 32, 36, 12.5f + k, c);
        pg_img_disc(im, 32, 29, 17.0f + k, c);
        pg_img_line(im, 17, 29, 9, 42, 9.5f + k, c);
        pg_img_disc(im, 45, 24, 7.5f + k, c); pg_img_disc(im, 46, 34, 7.0f + k, c); pg_img_disc(im, 43, 43, 6.2f + k, c);
    }
    pg_img_line(im, 41, 29, 50, 29, 1.8f, ARGB(170, edge)); pg_img_line(im, 41, 38, 50, 38, 1.8f, ARGB(170, edge));
    pg_img_disc(im, 28, 25, 6.5f, ARGB(110, 0xFFF0DC));
    pg_img_line(im, 28, 52, 28, 40, 3.0f, ARGB(90, 0xFFF0DC));
    pg_img_rect(im, 11, 10, 42, 5, ARGB(255, 0xFFFFFF));
    pg_img_rect(im, 13, 0, 38, 10, ARGB(255, 0x4A8DF0));
    pg_img_rect(im, 13, 0, 5, 10, ARGB(110, 0xFFFFFF));
    return finish(im);
}
static PG_Tex paint_flower(void)                     /* white petals with a golden heart; tinted pink / white / orange when drawn */
{
    PG_Image *im = pg_img_new(32, 32, 0);
    for (int i = 0; i < 8; i++) {
        float a = (float)i * PG_TAU / 8.0f;
        pg_img_disc(im, 16.0f + cosf(a) * 9.6f, 16.0f + sinf(a) * 9.6f, 5.6f, ARGB(255, 0xE0D8D8));
    }
    for (int i = 0; i < 8; i++) {
        float a = (float)i * PG_TAU / 8.0f;
        pg_img_disc(im, 16.0f + cosf(a) * 9.6f, 15.4f + sinf(a) * 9.6f, 5.2f, ARGB(255, 0xFFFFFF));
    }
    pg_img_disc(im, 16, 16, 6.2f, ARGB(255, 0xD79A2A));
    pg_img_disc(im, 15.6f, 15.4f, 5.2f, ARGB(255, 0xFFE27A));
    return finish(im);
}
static PG_Tex paint_stem(void)
{
    PG_Image *im = pg_img_new(8, 16, RGB(0x3FA047));
    pg_img_gradient_v(im, 0, 0, 8, 16, RGB(0x58BC54), RGB(0x2E8A3C));
    pg_img_rect(im, 1, 0, 2, 16, ARGB(70, 0xC8FFB0));
    return finish(im);
}

static PG_Tex paint_ball(void)
{
    static const uint32_t col[6] = { 0xFF5C8A, 0xFFFFFF, 0x36C9C0, 0xFFFFFF, 0xFFD23A, 0xFFFFFF };
    PG_Image *im = pg_img_new(64, 64, 0xFFFFFFFFu);
    for (int i = 0; i < 6; i++) pg_img_rect(im, i * 64 / 6, 0, 64 / 6 + 1, 64, RGB(col[i]));
    return finish(im);
}

static PG_Tex paint_bucket(void)
{
    PG_Image *im = pg_img_new(64, 32, RGB(0xFF6A94));
    pg_img_gradient_v(im, 0, 0, 64, 32, RGB(0xFF8CAA), RGB(0xE84878));
    pg_img_rect(im, 0, 0, 64, 6, ARGB(255, 0xFFFFFF));
    pg_img_rect(im, 0, 6, 64, 2, ARGB(255, 0xFFD23A));
    for (int i = 0; i < 4; i++) pg_img_star(im, 8.0f + (float)i * 16.0f, 20.0f, 5.5f, 2.4f, 5, 0.1f * (float)i, ARGB(255, 0xFFFFFF));
    for (int x = 0; x < 64; x++) pg_img_rect(im, x, 28 + (int)(1.6f * sinf((float)x * 0.5f)), 1, 4, ARGB(120, 0xFFFFFF));
    pg_img_noise(im, 5, 91);
    return finish(im);
}

static PG_Tex paint_starfish(void)
{
    PG_Image *im = pg_img_new(32, 32, 0);
    pg_img_star(im, 16, 17, 15, 6.5f, 5, 0, ARGB(255, 0xE8742C));
    pg_img_star(im, 16, 16, 13, 5.5f, 5, 0, ARGB(255, 0xFF9A3C));
    for (int i = 0; i < 5; i++) {
        float a = (float)i * PG_TAU / 5.0f - PG_PI * 0.5f;
        for (int k = 1; k < 4; k++) pg_img_disc(im, 16.0f + cosf(a) * (float)k * 3.4f, 16.0f + sinf(a) * (float)k * 3.4f, 0.9f, ARGB(220, 0xFFE0A8));
    }
    return finish(im);
}

static PG_Tex paint_sheen(void)                      /* glass streaks over the window (additive) */
{
    PG_Image *im = pg_img_new(64, 64, 0);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            float s = (float)(x + y), a = 0;
            float d1 = fabsf(s - 46.0f), d2 = fabsf(s - 70.0f);
            if (d1 < 6) a = 0.40f * (1.0f - d1 / 6.0f);
            if (d2 < 3.5f) a += 0.34f * (1.0f - d2 / 3.5f);
            im->px[y * 64 + x] = PG_ARGB((uint32_t)(a * 255.0f), 255, 255, 255);
        }
    PG_Tex t = pg3d_texture(im);
    pg_img_free(im);
    return t;
}

static PG_Tex paint_puff(void)                       /* dust puff for landing sand */
{
    PG_Image *im = pg_img_new(32, 32, 0x00FFFFFFu);
    for (int j = 0; j < 32; j++) for (int i = 0; i < 32; i++) {
        float d = sqrtf(((float)i - 15.5f) * ((float)i - 15.5f) + ((float)j - 15.5f) * ((float)j - 15.5f)) / 16.0f;
        float a = d >= 1 ? 0 : (1 - d) * (1 - d) * 0.9f;
        im->px[j * 32 + i] = PG_ARGB((uint32_t)(a * 255.0f), 255, 255, 255);
    }
    PG_Tex t = pg3d_texture(im);
    pg_img_free(im);
    return t;
}

/* ------------------------------------------------------------------- meshes */
typedef struct { PG_Vertex *v; uint16_t *ix; int nv, ni; } Bld;

static int bld_v(Bld *b, PG_V3 p, PG_V3 n, float u, float v)
{
    PG_Vertex *o = &b->v[b->nv];
    o->x = p.x; o->y = p.y; o->z = p.z; o->nx = n.x; o->ny = n.y; o->nz = n.z; o->u = u; o->v = v; o->c = pg_rgb(255, 255, 255);
    return b->nv++;
}

static void bld_t(Bld *b, int i0, int i1, int i2)    /* fix the winding to agree with the normals */
{
    const PG_Vertex *a = &b->v[i0], *c = &b->v[i1], *d = &b->v[i2];
    PG_V3 g = pg_v3_cross(pg_v3(c->x - a->x, c->y - a->y, c->z - a->z), pg_v3(d->x - a->x, d->y - a->y, d->z - a->z));
    PG_V3 n = pg_v3(a->nx + c->nx + d->nx, a->ny + c->ny + d->ny, a->nz + c->nz + d->nz);
    if (pg_v3_dot(g, g) < 1e-12f) return;
    if (pg_v3_dot(g, n) < 0) { int t = i1; i1 = i2; i2 = t; }
    b->ix[b->ni++] = (uint16_t)i0; b->ix[b->ni++] = (uint16_t)i1; b->ix[b->ni++] = (uint16_t)i2;
}

static PG_Mesh *bld_done(Bld *b)
{
    PG_Mesh *m = pg_mesh_build(b->v, b->nv, b->ix, b->ni);
    free(b->v); free(b->ix);
    return m;
}

static Bld bld_new(int nv, int ni)
{
    Bld b = { calloc((size_t)nv, sizeof(PG_Vertex)), calloc((size_t)ni, sizeof(uint16_t)), 0, 0 };
    if (!b.v || !b.ix) pg_fatal("out of memory (art)");
    return b;
}

/* a quad a,b,c,d (any winding), uv corners (a=u0,v1  b=u1,v1  c=u1,v0  d=u0,v0) */
static PG_Mesh *mesh_quad3(PG_V3 a, PG_V3 b, PG_V3 c, PG_V3 d, PG_V3 n)
{
    Bld bl = bld_new(4, 6);
    int i0 = bld_v(&bl, a, n, 0, 1), i1 = bld_v(&bl, b, n, 1, 1), i2 = bld_v(&bl, c, n, 1, 0), i3 = bld_v(&bl, d, n, 0, 0);
    bld_t(&bl, i0, i1, i2); bld_t(&bl, i0, i2, i3);
    return bld_done(&bl);
}

/* a glossy rainbow ribbon (vertex colours, white texture) for the front of the base */
static PG_Mesh *mesh_rainbow(float half_w, float half_h, int segs)
{
    Bld b = bld_new((segs + 1) * 2, segs * 6);
    for (int i = 0; i <= segs; i++) {
        float t = (float)i / (float)segs, x = -half_w + 2.0f * half_w * t;
        PG_Color top = pg_hsv(t * 300.0f, 0.55f, 1.0f), bot = pg_hsv(t * 300.0f, 0.8f, 0.82f);
        int a = bld_v(&b, pg_v3(x, half_h, 0), pg_v3(0, 0, 1), t, 0), c = bld_v(&b, pg_v3(x, -half_h, 0), pg_v3(0, 0, 1), t, 1);
        b.v[a].c = top; b.v[c].c = bot;
    }
    for (int i = 0; i < segs; i++) { int a = i * 2; bld_t(&b, a, a + 1, a + 3); bld_t(&b, a, a + 3, a + 2); }
    return bld_done(&b);
}

static PG_Mesh *mesh_frustum(float rb, float rt, float hh, int segs)
{
    Bld b = bld_new((segs + 1) * 2, segs * 6);
    float slope = (rb - rt) / (2.0f * hh);
    for (int i = 0; i <= segs; i++) {
        float a = PG_TAU * (float)i / (float)segs, c = cosf(a), s = sinf(a);
        PG_V3 n = pg_v3_norm(pg_v3(c, slope, s));
        bld_v(&b, pg_v3(rt * c, hh, rt * s), n, (float)i / (float)segs, 0);
        bld_v(&b, pg_v3(rb * c, -hh, rb * s), n, (float)i / (float)segs, 1);
    }
    for (int i = 0; i < segs; i++) { int a = i * 2; bld_t(&b, a, a + 1, a + 3); bld_t(&b, a, a + 3, a + 2); }
    return bld_done(&b);
}

/* the button cap: a round face carrying the picture, and plain red sides (the sides sample a flat red spot of the picture) */
static PG_Mesh *mesh_button_cap(float r, float hh, int segs)
{
    Bld b = bld_new(segs + 2 + (segs + 1) * 2, segs * 3 + segs * 6);
    int c = bld_v(&b, pg_v3(0, hh, 0), pg_v3(0, 1, 0), 0.5f, 0.5f), first = b.nv;
    for (int i = 0; i <= segs; i++) {
        float a = PG_TAU * (float)i / (float)segs;
        bld_v(&b, pg_v3(r * cosf(a), hh, r * sinf(a)), pg_v3(0, 1, 0), 0.5f + 0.5f * 0.97f * cosf(a), 0.5f + 0.5f * 0.97f * sinf(a));
    }
    for (int i = 0; i < segs; i++) bld_t(&b, c, first + i, first + i + 1);
    int side = b.nv;
    for (int i = 0; i <= segs; i++) {
        float a = PG_TAU * (float)i / (float)segs, cs = cosf(a), sn = sinf(a);
        bld_v(&b, pg_v3(r * cs, hh, r * sn), pg_v3(cs, 0, sn), 0.10f, 0.50f);
        bld_v(&b, pg_v3(r * cs, -hh, r * sn), pg_v3(cs, 0, sn), 0.10f, 0.50f);
    }
    for (int i = 0; i < segs; i++) { int a = side + i * 2; bld_t(&b, a, a + 1, a + 3); bld_t(&b, a, a + 3, a + 2); }
    return bld_done(&b);
}

static PG_Mesh *mesh_disc(float r, int segs)         /* flat fan facing +y */
{
    Bld b = bld_new(segs + 2, segs * 3);
    int c = bld_v(&b, pg_v3(0, 0, 0), pg_v3(0, 1, 0), 0.5f, 0.5f), first = b.nv;
    for (int i = 0; i <= segs; i++) {
        float a = PG_TAU * (float)i / (float)segs;
        bld_v(&b, pg_v3(r * cosf(a), 0, r * sinf(a)), pg_v3(0, 1, 0), 0.5f + 0.5f * cosf(a), 0.5f + 0.5f * sinf(a));
    }
    for (int i = 0; i < segs; i++) bld_t(&b, c, first + i, first + i + 1);
    return bld_done(&b);
}

static PG_Mesh *mesh_leaf(float len, float half_w, float droop)      /* a frond facing +z: axis along +x, drooping down */
{
    enum { SEG = 5 };
    Bld b = bld_new((SEG + 1) * 2, SEG * 6);
    for (int i = 0; i < SEG + 1; i++) {
        float t = (float)i / (float)SEG;
        PG_V3 n = pg_v3(0, 0, 1);
        bld_v(&b, pg_v3(len * t, -droop * t * t - half_w, 0), n, t, 1);
        bld_v(&b, pg_v3(len * t, -droop * t * t + half_w, 0), n, t, 0);
    }
    for (int i = 0; i < SEG; i++) { int a = i * 2; bld_t(&b, a, a + 1, a + 3); bld_t(&b, a, a + 3, a + 2); }
    return bld_done(&b);
}

void art_init(Art *a)
{
    /* textures */
    a->wood_h = paint_wood(128, 16, 0, 0xE59A4A, 0xB9722E);
    a->wood_v = paint_wood(16, 128, 1, 0xE59A4A, 0xB9722E);
    a->post = paint_post();
    a->wall = paint_wall();
    a->ground = paint_ground();
    a->sea = paint_sea();
    a->rays = paint_rays();
    for (int i = 0; i < 3; i++) a->sun[i] = paint_sun(i);
    a->cloud = paint_cloud();
    a->trunk = paint_trunk(); a->leaf = paint_leaf();
    a->btn = paint_button();
    a->btnbase = paint_stripes_tex(32, 8, 4, 0xFFD34A, 0xE59A18);
    a->crate = paint_crate();
    a->hand = paint_hand();
    a->flower = paint_flower();
    a->stem = paint_stem();
    a->ball = paint_ball();
    a->bucket = paint_bucket();
    a->starfish = paint_starfish();
    a->sheen = paint_sheen();
    a->puff = paint_puff();

    /* the frame: butt-jointed wooden bars (front half of the depth) with toy-block corner posts */
    const float hw = (WX1 - WX0) * 0.5f + BAR_T, hh = (WYT - WYB) * 0.5f;
    a->bar_top = pg_mesh_bevel_box(hw, BAR_T * 0.5f, ZF * 0.5f, 8.0f);
    a->bar_bot = pg_mesh_bevel_box(hw, BAR_B * 0.5f, ZF * 0.5f, 9.0f);
    a->bar_l = pg_mesh_bevel_box(BAR_T * 0.5f, hh, ZF * 0.5f, 8.0f);
    a->bar_r = pg_mesh_bevel_box(BAR_T * 0.5f, hh, ZF * 0.5f, 8.0f);
    a->post_t = pg_mesh_bevel_box(BAR_T * 0.5f + 7.0f, BAR_T * 0.5f + 7.0f, ZF * 0.5f + 9.0f, 8.0f);
    a->post_b = pg_mesh_bevel_box(BAR_T * 0.5f + 7.0f, BAR_B * 0.5f + 7.0f, ZF * 0.5f + 9.0f, 9.0f);

    /* the box behind the sand: back wall and the four inner walls */
    a->panel = pg_mesh_quad((WX1 - WX0) * 0.5f, hh);
    pg_mesh_gradient_y(a->panel, pg_rgb(255, 255, 255), pg_rgb(196, 204, 232));
    a->tun_l = mesh_quad3(pg_v3(WX0, WYB, -ZB), pg_v3(WX0, WYB, 0), pg_v3(WX0, WYT, 0), pg_v3(WX0, WYT, -ZB), pg_v3(1, 0, 0));
    a->tun_r = mesh_quad3(pg_v3(WX1, WYB, 0), pg_v3(WX1, WYB, -ZB), pg_v3(WX1, WYT, -ZB), pg_v3(WX1, WYT, 0), pg_v3(-1, 0, 0));
    a->tun_t = mesh_quad3(pg_v3(WX0, WYT, 0), pg_v3(WX1, WYT, 0), pg_v3(WX1, WYT, -ZB), pg_v3(WX0, WYT, -ZB), pg_v3(0, -1, 0));
    a->tun_b = mesh_quad3(pg_v3(WX0, WYB, -ZB), pg_v3(WX1, WYB, -ZB), pg_v3(WX1, WYB, 0), pg_v3(WX0, WYB, 0), pg_v3(0, 1, 0));

    /* scenery */
    a->ground_tile = pg_mesh_floor(250, 250, 4, 4);
    a->sea_quad = pg_mesh_quad(1600, 200);
    a->trunk_seg = pg_mesh_cylinder(10.0f, 20.0f, 10);
    a->leaf_m = mesh_leaf(112.0f, 24.0f, 34.0f);
    a->coconut_m = pg_mesh_sphere(8.0f, 12, 8);
    a->crate_m = pg_mesh_bevel_box(62.0f, 58.0f, 42.0f, 11.0f);
    a->btn_cap = mesh_button_cap(47.0f, 9.0f, 24);
    a->btn_ring = pg_mesh_cylinder(55.0f, 5.0f, 24);
    a->ball_m = pg_mesh_sphere(30.0f, 14, 10);
    a->bucket_m = mesh_frustum(21.0f, 29.0f, 24.0f, 14);
    a->bucket_cap = mesh_disc(27.0f, 14);
    a->stem_m = pg_mesh_cylinder(2.4f, 20.0f, 6);
    a->decal = pg_mesh_quad(1.0f, 1.0f);
    a->spade_h = pg_mesh_bevel_box(3.0f, 30.0f, 3.0f, 1.2f);
    a->spade_b = pg_mesh_bevel_box(10.0f, 13.0f, 2.2f, 1.6f);
    a->slot_m = a->decal;
    a->trim = mesh_rainbow((WX1 - WX0) * 0.5f - 12.0f, 5.5f, 24);
}

void art_shutdown(Art *a)
{
    PG_Mesh **list[] = { &a->bar_top, &a->bar_bot, &a->bar_l, &a->bar_r, &a->post_t, &a->post_b, &a->panel, &a->tun_l, &a->tun_r, &a->tun_t, &a->tun_b,
                         &a->ground_tile, &a->sea_quad, &a->trunk_seg, &a->leaf_m, &a->coconut_m, &a->crate_m, &a->btn_cap, &a->btn_ring, &a->ball_m,
                         &a->bucket_m, &a->bucket_cap, &a->stem_m, &a->decal, &a->spade_h, &a->spade_b, &a->trim };
    for (size_t i = 0; i < sizeof list / sizeof *list; i++) { pg_mesh_free(*list[i]); *list[i] = NULL; }
    a->slot_m = NULL;
}
