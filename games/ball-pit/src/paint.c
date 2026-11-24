/* paint.c - every texture of the Ball Pit is painted here at init (no image files).
 *
 * Balls are painted in lat/long ("equirect") layout, 128x64, but the patterns are computed in 3D from the
 * direction on the sphere, so dots, stars and hearts stay round instead of pinching at the poles.
 * Everything goes through pg_img_ps1() for the chunky 15-bit dithered PS1 look (except soft glows). */
#include "pit.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define BW 128
#define BH 64

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline uint32_t C(int r, int g, int b) { return PG_ARGB(255, r, g, b); }

static PG_Tex finish(PG_Image *im, bool dither)
{
    if (dither) pg_img_ps1(im);
    PG_Tex t = pg3d_texture(im);
    pg_img_free(im);
    return t;
}

/* ------------------------------------------------------------------ sphere patterns */
typedef struct { PG_V3 c, a, b; } Anchor;      /* a symbol centre on the sphere and its tangent frame */
static Anchor ico[12], oct[6];

static void anchor_make(Anchor *an, PG_V3 c, float roll)
{
    c = pg_v3_norm(c);
    PG_V3 hint = fabsf(c.y) > 0.9f ? pg_v3(0, 0, 1) : pg_v3(0, 1, 0);
    PG_V3 a = pg_v3_norm(pg_v3_cross(hint, c)), b = pg_v3_cross(c, a);
    float cs = cosf(roll), sn = sinf(roll);
    an->c = c;
    an->a = pg_v3_add(pg_v3_scale(a, cs), pg_v3_scale(b, sn));
    an->b = pg_v3_add(pg_v3_scale(b, cs), pg_v3_scale(a, -sn));
}

static void anchors_init(void)
{
    const float g = 1.6180339f;
    static const float ic[12][3] = { { 0, 1, g }, { 0, -1, g }, { 0, 1, -g }, { 0, -1, -g }, { 1, g, 0 }, { -1, g, 0 },
                                     { 1, -g, 0 }, { -1, -g, 0 }, { g, 0, 1 }, { -g, 0, 1 }, { g, 0, -1 }, { -g, 0, -1 } };
    static const float oc[6][3] = { { 0, 0, 1 }, { 0, 0, -1 }, { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 } };
    for (int i = 0; i < 12; i++) anchor_make(&ico[i], pg_v3(ic[i][0], ic[i][1], ic[i][2]), 0.9f * (float)i);
    for (int i = 0; i < 6; i++)  anchor_make(&oct[i], pg_v3(oc[i][0], oc[i][1], oc[i][2]), 0.35f + 0.8f * (float)i);
}

static const Anchor *nearest(const Anchor *list, int n, PG_V3 p, float *cos_d)
{
    const Anchor *best = list;
    float bd = -2;
    for (int i = 0; i < n; i++) {
        float d = pg_v3_dot(p, list[i].c);
        if (d > bd) { bd = d; best = &list[i]; }
    }
    *cos_d = bd;
    return best;
}

/* planar coordinates (radians of arc) of p around the anchor, in the anchor's own frame */
static void local_xy(const Anchor *an, PG_V3 p, float cos_d, float *x, float *y)
{
    float alpha = acosf(clampf(cos_d, -1, 1));
    float ax = pg_v3_dot(p, an->a), ay = pg_v3_dot(p, an->b), l = sqrtf(ax * ax + ay * ay);
    if (l < 1e-6f) { *x = *y = 0; return; }
    *x = ax / l * alpha; *y = ay / l * alpha;
}

static bool in_star(float x, float y, float r_in)        /* 5 points, outer radius 1, tip up */
{
    float rho = sqrtf(x * x + y * y);
    if (rho < 1e-5f) return true;
    const float k = 72.0f * PG_PI / 180.0f, half = 36.0f * PG_PI / 180.0f;
    float m = fabsf(fmodf(fabsf(atan2f(x, y)) + half, k) - half);       /* angle to the nearest tip, 0..36 deg */
    float px = rho * cosf(m), py = rho * sinf(m);
    float vx = r_in * cosf(half) - 1.0f, vy = r_in * sinf(half);        /* edge from the tip to the inner corner */
    return vx * py - vy * (px - 1.0f) > 0.0f;
}

static bool in_heart(float x, float y)
{
    float a = x * x + y * y - 1.0f;
    return a * a * a - x * x * y * y * y <= 0.0f;
}

static const uint32_t rainbow7[7] = { PG_RGB24(0xF03C3C), PG_RGB24(0xFF962B), PG_RGB24(0xFFDC32), PG_RGB24(0x46C85A),
                                      PG_RGB24(0x36BEE6), PG_RGB24(0x3C6EEB), PG_RGB24(0xAA5AE6) };

static uint32_t pat_beach(float u, float v)
{
    static const uint32_t col[6] = { PG_RGB24(0xE8322D), PG_RGB24(0xFFFFFF), PG_RGB24(0x2F78E8),
                                     PG_RGB24(0xFFFFFF), PG_RGB24(0xFFC62B), PG_RGB24(0xFFFFFF) };
    float su = u * 6.0f, fu = su - floorf(su);
    uint32_t c = col[(int)su % 6];
    if (v < 0.10f || v > 0.90f) return (v < 0.045f || v > 0.955f) ? col[0] : col[1];
    if (fu < 0.035f || fu > 0.965f) c = pg_argb_scale(c, 0.86f);
    return c;
}

static uint32_t pat_smiley(PG_V3 p)
{
    float side = p.z >= 0 ? 1.0f : -1.0f, x = p.x * side, y = p.y, rho = sqrtf(x * x + y * y);
    uint32_t c = pg_argb_lerp(C(255, 232, 96), C(255, 196, 40), rho);
    if (rho > 0.97f) return C(255, 190, 38);
    float ex = (fabsf(x) - 0.30f) / 0.075f, ey = (y - 0.22f) / 0.125f;
    if (ex * ex + ey * ey < 1.0f) return C(96, 52, 18);
    float mx = x, my = y - 0.10f, md = sqrtf(mx * mx + my * my);
    if (y < -0.10f && fabsf(x) < 0.47f && fabsf(md - 0.52f) < 0.045f) return C(150, 64, 24);
    float cx = fabsf(x) - 0.52f, cy = y + 0.14f;
    if (cx * cx + cy * cy < 0.011f) return pg_argb_lerp(c, C(255, 130, 120), 0.7f);
    return c;
}

static uint32_t ball_pattern(int kind, PG_V3 p, float u, float v)
{
    float cd, x, y;
    const Anchor *an;
    switch (kind) {
    case BT_PLAIN: {        /* tinted at draw time: a light belt so a plain ball visibly rolls */
        float d = fabsf(v - 0.5f);
        return d < 0.07f ? C(255, 255, 255) : d < 0.095f ? C(176, 176, 186) : C(224, 224, 230);
    }
    case BT_BEACH:   return pat_beach(u, v);
    case BT_STRIPES: {
        float s = v * 7.0f, f = s - floorf(s);
        int band = (int)s > 6 ? 6 : (int)s;
        return (f < 0.06f || f > 0.94f) ? pg_argb_scale(rainbow7[band], 0.88f) : rainbow7[band];
    }
    case BT_DOTS_RED:
        an = nearest(ico, 12, p, &cd);
        (void)an;
        return cd > cosf(0.30f) ? C(255, 255, 255) : C(238, 56, 64);
    case BT_DOTS_BLUE:
        an = nearest(ico, 12, p, &cd);
        (void)an;
        if (cd > cosf(0.27f)) return C(255, 214, 54);
        an = nearest(oct, 6, p, &cd);
        return cd > cosf(0.15f) ? C(255, 255, 255) : C(44, 112, 236);
    case BT_STARS_BLUE:
        an = nearest(oct, 6, p, &cd);
        local_xy(an, p, cd, &x, &y);
        return in_star(x / 0.66f, y / 0.66f, 0.5f) ? C(255, 218, 52) : C(40, 98, 226);
    case BT_STARS_PURPLE:
        an = nearest(ico, 12, p, &cd);
        local_xy(an, p, cd, &x, &y);
        return in_star(x / 0.44f, y / 0.44f, 0.48f) ? C(255, 255, 255) : C(150, 72, 222);
    case BT_HEARTS:
        an = nearest(ico, 12, p, &cd);
        local_xy(an, p, cd, &x, &y);
        return in_heart(x / 0.37f, y / 0.37f + 0.12f) ? C(232, 40, 88) : C(255, 190, 210);
    case BT_DAISY: {
        an = nearest(oct, 6, p, &cd);
        local_xy(an, p, cd, &x, &y);
        float rho = sqrtf(x * x + y * y) / 0.60f;
        if (rho < 0.27f) return C(255, 196, 40);
        float petal = 0.50f + 0.50f * fabsf(cosf(4.0f * atan2f(y, x)));
        return rho < petal ? C(255, 255, 255) : C(110, 204, 250);
    }
    case BT_SWIRL_RED: {
        float t = u * 5.0f + v * 3.4f; t -= floorf(t);
        return t < 0.5f ? C(236, 52, 62) : C(255, 255, 255);
    }
    case BT_SWIRL_GREEN: {
        float t = u * 4.0f - v * 2.6f; t -= floorf(t);
        return t < 0.5f ? C(52, 190, 92) : C(255, 232, 92);
    }
    case BT_CHECK:
        return (((int)(u * 10.0f) + (int)(v * 6.0f)) & 1) ? C(255, 255, 255) : C(54, 196, 98);
    default:
        return pat_smiley(p);
    }
}

static PG_V3 *sdir;                       /* 2x2 samples per texel, shared by all ball textures */

static PG_Tex paint_ball(int kind)
{
    PG_Image *im = pg_img_new(BW, BH, 0xFFFFFFFFu);
    for (int j = 0; j < BH; j++)
        for (int i = 0; i < BW; i++) {
            int r = 0, g = 0, b = 0;
            for (int s = 0; s < 4; s++) {
                float u = ((float)i + 0.25f + 0.5f * (float)(s & 1)) / BW, v = ((float)j + 0.25f + 0.5f * (float)(s >> 1)) / BH;
                uint32_t c = ball_pattern(kind, sdir[(j * BW + i) * 4 + s], u, v);
                r += (int)((c >> 16) & 255); g += (int)((c >> 8) & 255); b += (int)(c & 255);
            }
            im->px[j * BW + i] = PG_ARGB(255, r / 4, g / 4, b / 4);
        }
    return finish(im, true);
}

/* ------------------------------------------------------------------ playroom textures */
static PG_Tex paint_wallpaper(void)
{
    PG_Image *im = pg_img_new(64, 64, PG_RGB24(0xFFE9A8));
    for (int x = 0; x < 64; x += 16) pg_img_rect(im, x, 0, 8, 64, PG_ARGB(90, 255, 244, 196));
    for (int ox = -64; ox <= 64; ox += 64)
        for (int oy = -64; oy <= 64; oy += 64) {
            float fx = (float)ox, fy = (float)oy;
            pg_img_disc(im, 16 + fx, 16 + fy, 7.5f, PG_RGB24(0xFFF6D2));
            pg_img_disc(im, 48 + fx, 48 + fy, 7.5f, PG_RGB24(0xFFF6D2));
            pg_img_star(im, 48 + fx, 17 + fy, 7.0f, 3.0f, 5, 0.2f, PG_RGB24(0xFFC9A6));
            pg_img_star(im, 16 + fx, 49 + fy, 7.0f, 3.0f, 5, -0.2f, PG_RGB24(0xBFE3FF));
        }
    pg_img_noise(im, 4, 3);
    return finish(im, true);
}

static PG_Tex paint_quilt(void)             /* neutral grey-white padded vinyl: tinted per use */
{
    PG_Image *im = pg_img_new(64, 64, 0xFFFFFFFFu);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            float fx = (float)x + 0.5f, fy = (float)y + 0.5f;
            float a = fmodf((fx + fy) * 0.5f, 32.0f), b = fmodf((fx - fy) * 0.5f + 64.0f, 32.0f);
            float d = fmaxf(fabsf(a - 16.0f), fabsf(b - 16.0f)) / 16.0f;      /* 0 puff centre .. 1 seam */
            float lit = 0.5f - ((a - 16.0f) + (b - 16.0f)) / 64.0f;           /* soft top-left light on each puff */
            float s = 1.0f - 0.30f * d * d * d + 0.10f * (lit - 0.5f) * (1.0f - d) - (d > 0.90f ? 0.20f : 0.0f);
            int v = (int)(clampf(s, 0, 1) * 255.0f);
            im->px[y * 64 + x] = C(v, v, clampf((float)v * 1.03f, 0, 255));
        }
    for (int ox = 0; ox <= 64; ox += 32)           /* buttons where the quilting seams cross: corners and centre */
        for (int oy = 0; oy <= 64; oy += 32) {
            if (((ox + oy) / 32) & 1) continue;
            pg_img_disc(im, (float)ox, (float)oy, 4.2f, C(176, 176, 196));
            pg_img_disc(im, (float)ox - 0.8f, (float)oy - 0.8f, 2.8f, C(255, 255, 255));
        }
    return finish(im, true);
}

static PG_Tex paint_wood(void)
{
    static const uint32_t plank[4] = { PG_RGB24(0xE3AC62), PG_RGB24(0xD79B52), PG_RGB24(0xEAB872), PG_RGB24(0xDCA45A) };
    PG_Image *im = pg_img_new(64, 64, 0xFFFFFFFFu);
    uint32_t s = 7u;
    for (int p = 0; p < 4; p++) {
        pg_img_rect(im, 0, p * 16, 64, 16, plank[p]);
        for (int k = 0; k < 5; k++) {
            s = s * 1664525u + 1013904223u;
            int gy = p * 16 + 2 + (int)((s >> 16) % 12u), gx = (int)((s >> 8) % 20u), gl = 24 + (int)((s >> 4) % 36u);
            pg_img_rect(im, gx, gy, gl, 1, PG_ARGB(70, 150, 90, 36));
        }
        pg_img_rect(im, 0, p * 16, 64, 1, PG_ARGB(190, 130, 78, 34));
        pg_img_rect(im, 0, p * 16 + 15, 64, 1, PG_ARGB(60, 255, 230, 170));
        pg_img_disc(im, 4.0f, (float)(p * 16 + 8), 1.6f, PG_RGB24(0x9A6430));
        pg_img_disc(im, 60.0f, (float)(p * 16 + 8), 1.6f, PG_RGB24(0x9A6430));
    }
    pg_img_noise(im, 6, 5);
    return finish(im, true);
}

static void toy_block(PG_Image *im, int x, int y, int sz, uint32_t base, int symbol)
{
    pg_img_rect(im, x, y, sz, sz, base);
    pg_img_rect(im, x, y, sz, 2, pg_argb_lerp(base, 0xFFFFFFFFu, 0.45f));
    pg_img_rect(im, x, y, 2, sz, pg_argb_lerp(base, 0xFFFFFFFFu, 0.30f));
    pg_img_rect(im, x, y + sz - 2, sz, 2, pg_argb_scale(base, 0.72f));
    pg_img_rect(im, x + sz - 2, y, 2, sz, pg_argb_scale(base, 0.80f));
    pg_img_frame(im, x, y, sz, sz, 1, PG_ARGB(120, 60, 30, 10));
    float cx = (float)x + (float)sz * 0.5f, cy = (float)y + (float)sz * 0.5f, r = (float)sz * 0.28f;
    uint32_t w = C(255, 255, 255);
    switch (symbol) {
    case 0: pg_img_disc(im, cx, cy, r, w); break;
    case 1: pg_img_star(im, cx, cy + 0.5f, r * 1.25f, r * 0.55f, 5, 0, w); break;
    case 2: pg_img_rect(im, (int)(cx - r), (int)(cy - r), (int)(r * 2), (int)(r * 2), w); break;
    case 3: pg_img_star(im, cx, cy + r * 0.25f, r * 1.4f, r * 0.7f, 3, 0, w); break;
    default: pg_img_star(im, cx, cy, r * 1.35f, r * 0.95f, 4, 0.7854f, w); break;
    }
}

static PG_Tex paint_lip(void)               /* a row of painted toy blocks: 4 x 2 per tile */
{
    static const uint32_t col[8] = { PG_RGB24(0xE8402F), PG_RGB24(0xFFC21F), PG_RGB24(0x32B96A), PG_RGB24(0x2F7FE8),
                                     PG_RGB24(0x9A55D9), PG_RGB24(0xFF8A1F), PG_RGB24(0x1FB8C9), PG_RGB24(0xFF5FA2) };
    PG_Image *im = pg_img_new(128, 64, PG_RGB24(0xC8884A));
    for (int j = 0; j < 2; j++)
        for (int i = 0; i < 4; i++) toy_block(im, i * 32, j * 32, 32, col[(i + j * 4) & 7], (i * 2 + j * 3) % 5);
    pg_img_noise(im, 4, 9);
    return finish(im, true);
}

static PG_Tex paint_mat(void)               /* foam play-mat squares */
{
    static const uint32_t col[4] = { PG_RGB24(0xFF7B72), PG_RGB24(0xFFD65A), PG_RGB24(0x6ECF98), PG_RGB24(0x6AAAFF) };
    PG_Image *im = pg_img_new(64, 64, 0xFFFFFFFFu);
    for (int j = 0; j < 2; j++)
        for (int i = 0; i < 2; i++) {
            int x = i * 32, y = j * 32;
            uint32_t c = col[(i + j * 2) & 3];
            pg_img_rect(im, x, y, 32, 32, c);
            pg_img_rect(im, x, y, 32, 2, pg_argb_lerp(c, 0xFFFFFFFFu, 0.5f));
            pg_img_rect(im, x, y, 2, 32, pg_argb_lerp(c, 0xFFFFFFFFu, 0.35f));
            pg_img_rect(im, x, y + 30, 32, 2, pg_argb_scale(c, 0.68f));
            pg_img_rect(im, x + 30, y, 2, 32, pg_argb_scale(c, 0.78f));
            for (int k = 0; k < 4; k++) pg_img_disc(im, (float)(x + 8 + (k & 1) * 16), (float)(y + 8 + (k >> 1) * 16), 1.5f, pg_argb_scale(c, 0.88f));
        }
    pg_img_noise(im, 5, 11);
    return finish(im, true);
}

static PG_Tex paint_rainbow(void)           /* v runs across the tube: outer red .. inner violet */
{
    PG_Image *im = pg_img_new(16, 64, 0xFFFFFFFFu);
    static const uint32_t col[6] = { PG_RGB24(0xF03C3C), PG_RGB24(0xFF962B), PG_RGB24(0xFFDC32),
                                     PG_RGB24(0x46C85A), PG_RGB24(0x3C82EB), PG_RGB24(0x9650E0) };
    for (int j = 0; j < 64; j++) {
        int band = j * 6 / 64;
        uint32_t c = col[band];
        int in_band = j - band * 64 / 6;
        if (in_band < 1) c = pg_argb_lerp(c, 0xFFFFFFFFu, 0.25f);
        pg_img_rect(im, 0, j, 16, 1, c);
    }
    return finish(im, true);
}

static PG_Tex paint_cloud(void)
{
    PG_Image *im = pg_img_new(64, 32, 0x00FFFFFFu);
    static const float cx[5] = { 14, 25, 37, 49, 31 }, cy[5] = { 21, 15, 12, 19, 20 }, cr[5] = { 8, 10, 11, 8, 9 };
    for (int i = 0; i < 5; i++) pg_img_disc(im, cx[i], cy[i] + 2.2f, cr[i], PG_RGB24(0xC4D8F6));      /* shaded underside */
    pg_img_rect(im, 10, 20, 44, 8, PG_RGB24(0xC4D8F6));
    for (int i = 0; i < 5; i++) pg_img_disc(im, cx[i], cy[i], cr[i], PG_RGB24(0xFFFFFF));
    pg_img_rect(im, 10, 18, 44, 7, PG_RGB24(0xFFFFFF));
    return finish(im, true);
}

static PG_Tex paint_sun(void)
{
    PG_Image *im = pg_img_new(64, 64, 0x00FFFFFFu);
    pg_img_star(im, 32, 32, 31.0f, 23.0f, 12, 0.13f, PG_RGB24(0xFF9F2E));
    pg_img_disc(im, 32, 32, 21.0f, PG_RGB24(0xFFB52E));
    pg_img_disc(im, 32, 31, 19.5f, PG_RGB24(0xFFD93A));
    pg_img_disc(im, 27, 25, 7.0f, PG_RGB24(0xFFEE80));
    pg_img_disc(im, 25.5f, 29.0f, 2.2f, PG_RGB24(0x7A4216));
    pg_img_disc(im, 38.5f, 29.0f, 2.2f, PG_RGB24(0x7A4216));
    for (int k = 0; k < 8; k++) {                                          /* smile: a short arc below the eyes */
        float a0 = (0.30f + 0.30f * (float)k) * 1.0f, a1 = a0 + 0.30f;
        pg_img_line(im, 32.0f + 9.0f * cosf(a0), 31.0f + 8.0f * sinf(a0), 32.0f + 9.0f * cosf(a1), 31.0f + 8.0f * sinf(a1), 2.0f, PG_RGB24(0xB0561C));
    }
    pg_img_disc(im, 21.5f, 36.0f, 2.8f, PG_RGB24(0xFF9060));
    pg_img_disc(im, 42.5f, 36.0f, 2.8f, PG_RGB24(0xFF9060));
    return finish(im, true);
}

static PG_Tex paint_flag(void)              /* white pennant cloth: tinted per flag */
{
    PG_Image *im = pg_img_new(32, 32, 0xFFFFFFFFu);
    pg_img_gradient_v(im, 0, 0, 32, 32, C(255, 255, 255), C(226, 226, 236));
    pg_img_rect(im, 0, 0, 32, 3, C(206, 206, 220));
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 4; i++) pg_img_disc(im, 5.0f + (float)i * 7.5f + (float)(j & 1) * 3.5f, 8.0f + (float)j * 7.0f, 1.9f, C(255, 255, 255));
    return finish(im, true);
}

static PG_Tex paint_hand(void)              /* friendly cartoon glove, index finger up; the fingertip is at about (28,5) */
{
    PG_Image *im = pg_img_new(64, 64, 0x00FFFFFFu);
    uint32_t edge = C(150, 168, 222), fill = C(255, 255, 255), shade = C(214, 224, 248);
    for (int pass = 0; pass < 2; pass++) {
        float g = pass == 0 ? 2.0f : 0.0f;
        uint32_t c = pass == 0 ? edge : fill;
        pg_img_line(im, 28, 33, 28, 11, 11.0f + 2 * g, c);                 /* index finger */
        pg_img_line(im, 19, 40, 13, 32, 8.0f + 2 * g, c);                  /* thumb */
        pg_img_disc(im, 40.0f, 31.0f, 6.5f + g, c);                        /* curled fingers */
        pg_img_disc(im, 46.0f, 37.0f, 6.0f + g, c);
        pg_img_disc(im, 47.0f, 44.0f, 5.5f + g, c);
        pg_img_disc(im, 32.0f, 42.0f, 15.0f + g, c);                       /* palm */
        if (pass == 0) pg_img_rect(im, 19, 50, 28, 12, edge);
    }
    pg_img_disc(im, 36, 48, 9.0f, shade);                                  /* a little shading under the palm */
    pg_img_disc(im, 32, 39, 11.0f, fill);
    pg_img_line(im, 28, 33, 28, 11, 8.5f, fill);
    pg_img_disc(im, 26.0f, 9.0f, 2.2f, C(255, 255, 255));
    pg_img_rect(im, 20, 51, 26, 11, C(238, 64, 74));                      /* red cuff */
    pg_img_rect(im, 20, 51, 26, 3, C(255, 134, 140));
    pg_img_rect(im, 20, 59, 26, 3, C(188, 36, 52));
    return finish(im, true);
}

static PG_Tex paint_ring(void)              /* soft glowing ring for fingertips (additive) */
{
    PG_Image *im = pg_img_new(64, 64, 0x00FFFFFFu);
    for (int j = 0; j < 64; j++)
        for (int i = 0; i < 64; i++) {
            float dx = (float)i + 0.5f - 32.0f, dy = (float)j + 0.5f - 32.0f, d = sqrtf(dx * dx + dy * dy);
            float ring = expf(-((d - 26.0f) * (d - 26.0f)) / 7.0f);
            float inner = d < 26.0f ? 0.16f * (1.0f - d / 26.0f) + 0.05f : 0.0f;
            float a = clampf(ring * 0.95f + inner, 0, 1);
            if (d > 31.5f) a = 0;
            im->px[j * 64 + i] = PG_ARGB((uint32_t)(a * 255.0f), 255, 255, 255);
        }
    return finish(im, false);
}

void paint_textures(PitTex *t)
{
    anchors_init();
    sdir = malloc((size_t)BW * BH * 4 * sizeof *sdir);
    if (!sdir) pg_fatal("out of memory (ball textures)");
    for (int j = 0; j < BH; j++)
        for (int i = 0; i < BW; i++)
            for (int s = 0; s < 4; s++) {
                float u = ((float)i + 0.25f + 0.5f * (float)(s & 1)) / BW, v = ((float)j + 0.25f + 0.5f * (float)(s >> 1)) / BH;
                float th = PG_PI * v, ph = -PG_PI + PG_TAU * u;
                sdir[(j * BW + i) * 4 + s] = pg_v3(sinf(th) * sinf(ph), cosf(th), sinf(th) * cosf(ph));
            }
    for (int k = 0; k < BT_COUNT; k++) t->ball[k] = paint_ball(k);
    free(sdir);
    sdir = NULL;

    t->wallpaper = paint_wallpaper();  t->quilt = paint_quilt();  t->wood = paint_wood();   t->lip = paint_lip();
    t->mat = paint_mat();              t->rainbow = paint_rainbow(); t->cloud = paint_cloud(); t->sun = paint_sun();
    t->flag = paint_flag();            t->hand = paint_hand();      t->ring = paint_ring();
}
