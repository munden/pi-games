/* art.c - procedural painting of every texture (see art.h).  Small chunky textures, 15-bit dithered. */
#include "art.h"

#include <math.h>
#include <string.h>

static const uint32_t CANDY[JB_STYLES] = { 0xFF4F78, 0x9BEB52, 0xA46EFF, 0xFFDC3E, 0x3FB8FF, 0xFF8E3A };

PG_Color art_candy(int style) { return pg_hex(CANDY[(unsigned)style % JB_STYLES]); }

/* ----------------------------------------------------------------------- helpers */
static inline float c01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static inline float sm(float a, float b, float v) { float t = c01((v - a) / (b - a)); return t * t * (3.0f - 2.0f * t); }

static uint32_t rng_state = 7u;
static float rnd(void) { rng_state = rng_state * 1664525u + 1013904223u; return (float)((rng_state >> 8) & 0xFFFF) / 65536.0f; }

static uint32_t mixc(uint32_t a, uint32_t b, float t) { return pg_argb_lerp(a, b, t); }
static uint32_t with_a(uint32_t c, int a) { return (c & 0xFFFFFFu) | ((uint32_t)a << 24); }
static const uint32_t WHITE = 0xFFFFFFFFu;

/* straight-alpha "over" of colour c (with extra coverage) onto one pixel */
static void put(PG_Image *im, int x, int y, uint32_t c, float cov)
{
    if (x < 0 || y < 0 || x >= im->w || y >= im->h || cov <= 0) return;
    float sa = (float)(c >> 24) * (1.0f / 255.0f) * cov;
    if (sa <= 0) return;
    uint32_t *d = &im->px[y * im->w + x];
    uint32_t dv = *d;
    float da = (float)(dv >> 24) * (1.0f / 255.0f), oa = sa + da * (1.0f - sa);
    uint32_t out = (uint32_t)(oa * 255.0f + 0.5f) << 24;
    for (int sh = 0; sh <= 16; sh += 8) {
        float sc = (float)((c >> sh) & 255), dc = (float)((dv >> sh) & 255);
        out |= (uint32_t)((sc * sa + dc * da * (1.0f - sa)) / oa + 0.5f) << sh;
    }
    *d = out;
}

/* anti-aliased (rotated) ellipse with a vertical gradient from top to bottom colour */
static void ell(PG_Image *im, float cx, float cy, float rx, float ry, float ang, uint32_t top, uint32_t bot)
{
    float ca = cosf(ang), sa = sinf(ang), R = fmaxf(rx, ry) + 2.0f, mn = fminf(rx, ry);
    for (int y = (int)floorf(cy - R); y <= (int)ceilf(cy + R); y++)
        for (int x = (int)floorf(cx - R); x <= (int)ceilf(cx + R); x++) {
            float px = (float)x + 0.5f - cx, py = (float)y + 0.5f - cy;
            float lx = px * ca + py * sa, ly = -px * sa + py * ca;
            float d = sqrtf((lx / rx) * (lx / rx) + (ly / ry) * (ly / ry));
            float cov = c01((1.0f - d) * mn + 0.5f);
            if (cov > 0) put(im, x, y, mixc(top, bot, c01(0.5f + ly / (2.0f * ry))), cov);
        }
}

static void curve(PG_Image *im, float x0, float y0, float cx, float cy, float x1, float y1, float th, uint32_t c)
{
    float px = x0, py = y0;
    for (int i = 1; i <= 18; i++) {
        float t = (float)i / 18.0f, u = 1.0f - t;
        float x = u * u * x0 + 2 * u * t * cx + t * t * x1, y = u * u * y0 + 2 * u * t * cy + t * t * y1;
        pg_img_line(im, px, py, x, y, th, c);
        px = x; py = y;
    }
}

static PG_Tex upload(PG_Image *im, bool dither)
{
    if (dither) pg_img_ps1(im);
    PG_Tex t = pg3d_texture(im);
    pg_img_free(im);
    return t;
}

static void blit(PG_Image *dst, const PG_Image *src, int ox, int oy)
{
    for (int y = 0; y < src->h; y++)
        for (int x = 0; x < src->w; x++) dst->px[(oy + y) * dst->w + ox + x] = src->px[y * src->w + x];
}

PG_Tex art_cell(PG_Tex s, int idx, int cols, int rows)
{
    int cx = idx % cols, cy = idx / cols;
    float du = (s.u1 - s.u0) / (float)cols, dv = (s.v1 - s.v0) / (float)rows;
    float e = 0.04f / 1024.0f;                       /* a hair inside the cell */
    PG_Tex t = { s.u0 + du * (float)cx + e, s.v0 + dv * (float)cy + e, s.u0 + du * (float)(cx + 1) - e, s.v0 + dv * (float)(cy + 1) - e };
    return t;
}

/* ------------------------------------------------------------------ jelly bodies */
static PG_Tex paint_jelly(uint32_t base, uint32_t seed)
{
    rng_state = seed * 2654435761u + 17u;
    uint32_t mid = PG_RGB24(base);
    uint32_t pale = mixc(mid, WHITE, 0.52f);
    uint32_t deep = mixc(mid, PG_RGB24(0x3A1257), 0.24f);           /* shadows lean violet, never grey */
    PG_Image *im = pg_img_new(64, 64, mid);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            float dx = ((float)x + 0.5f - 32.0f) / 28.6f, dy = ((float)y + 0.5f - 32.0f) / 28.6f;
            float d = sqrtf(dx * dx + dy * dy);
            uint32_t c = d < 0.60f ? mixc(pale, mid, sm(0.0f, 0.60f, d)) : mixc(mid, deep, sm(0.60f, 1.10f, d));
            float gx = ((float)x + 0.5f - 24.0f) / 21.0f, gy = ((float)y + 0.5f - 22.0f) / 18.0f;
            float g = c01(1.0f - sqrtf(gx * gx + gy * gy));
            im->px[y * 64 + x] = mixc(c, WHITE, g * g * 0.30f) | 0xFF000000u;
        }
    for (int k = 0; k < 11; k++) {                                   /* air bubbles trapped in the jelly */
        float a = rnd() * 6.2832f, rr = sqrtf(rnd()) * 23.0f, r = 1.7f + rnd() * 3.4f;
        float bx = 32.0f + cosf(a) * rr, by = 32.0f + sinf(a) * rr;
        pg_img_disc(im, bx, by, r, with_a(0xFFFFFF, 52));
        pg_img_ring(im, bx, by, r, 1.0f, with_a(0xFFFFFF, 96));
        pg_img_disc(im, bx - r * 0.35f, by - r * 0.38f, fmaxf(0.8f, r * 0.30f), with_a(0xFFFFFF, 200));
    }
    for (int k = 0; k < 14; k++) {                                   /* darker flecks */
        float a = rnd() * 6.2832f, rr = sqrtf(rnd()) * 24.0f;
        pg_img_disc(im, 32.0f + cosf(a) * rr, 32.0f + sinf(a) * rr, 0.8f + rnd() * 0.9f, with_a(deep, 80));
    }
    for (int k = 0; k < 5; k++) {                                    /* sugar sparkles */
        float a = rnd() * 6.2832f, rr = sqrtf(rnd()) * 22.0f, sx = 32.0f + cosf(a) * rr, sy = 32.0f + sinf(a) * rr;
        pg_img_rect(im, (int)sx - 1, (int)sy, 3, 1, with_a(0xFFFFFF, 170));
        pg_img_rect(im, (int)sx, (int)sy - 1, 1, 3, with_a(0xFFFFFF, 170));
    }
    pg_img_noise(im, 4, seed + 3u);
    return upload(im, true);
}

/* ------------------------------------------------------------------------- faces */
static PG_Tex paint_face(void)
{
    const uint32_t ink = PG_RGB24(0x2A2147), berry = PG_RGB24(0x7C2551), berry_d = PG_RGB24(0x56163F), pink = PG_RGB24(0xFF88AA);
    PG_Image *sheet = pg_img_new(128, 64, 0x00FFFFFFu);
    for (int cell = 0; cell < 8; cell++) {
        PG_Image *c = pg_img_new(32, 32, 0x00FFFFFFu);
        switch (cell) {
        case FACE_SCLERA:
            ell(c, 16, 16.5f, 14.4f, 16.4f, 0, with_a(0xAEB9E3, 200), with_a(0xAEB9E3, 200));
            ell(c, 16, 16.5f, 13.0f, 15.0f, 0, WHITE, PG_RGB24(0xDCE6FA));
            break;
        case FACE_PUPIL:
            ell(c, 16, 16, 11.8f, 11.8f, 0, ink, ink);
            ell(c, 16, 19.2f, 9.4f, 7.8f, 0, PG_RGB24(0x4C3B86), PG_RGB24(0x7E69CE));
            ell(c, 16, 14.0f, 9.0f, 8.0f, 0, ink, with_a(0x2A2147, 0));
            pg_img_disc(c, 11.6f, 11.4f, 3.7f, WHITE);
            pg_img_disc(c, 20.8f, 21.2f, 1.8f, with_a(0xFFFFFF, 205));
            break;
        case FACE_BLINK:
            curve(c, 5.5f, 14.0f, 16, 23.0f, 26.5f, 14.0f, 3.4f, ink);
            break;
        case FACE_HAPPY:
            curve(c, 5.5f, 21.0f, 16, 5.0f, 26.5f, 21.0f, 3.8f, ink);
            break;
        case FACE_SMILE:
            curve(c, 5.0f, 10.0f, 16, 25.0f, 27.0f, 10.0f, 3.3f, berry);
            pg_img_disc(c, 5.0f, 10.0f, 1.9f, berry); pg_img_disc(c, 27.0f, 10.0f, 1.9f, berry);
            break;
        case FACE_OH:
            ell(c, 16, 16.5f, 7.6f, 9.8f, 0, berry, berry);
            ell(c, 16, 14.8f, 5.8f, 7.4f, 0, berry_d, with_a(0x7C2551, 0));
            ell(c, 16, 22.0f, 4.7f, 3.2f, 0, pink, PG_RGB24(0xFF6F96));
            break;
        case FACE_GRIN:
            for (int y = 0; y < 32; y++)
                for (int x = 0; x < 32; x++) {
                    float px = (float)x + 0.5f, py = (float)y + 0.5f;
                    float ytop = 9.0f + 2.6f * (1.0f - ((px - 16.0f) / 12.0f) * ((px - 16.0f) / 12.0f));
                    float e = sqrtf(((px - 16.0f) / 12.2f) * ((px - 16.0f) / 12.2f) + ((py - 9.0f) / 15.0f) * ((py - 9.0f) / 15.0f));
                    float cov = c01((1.0f - e) * 12.0f + 0.5f) * c01(py - ytop + 0.5f);
                    if (cov <= 0) continue;
                    uint32_t col = mixc(berry_d, berry, c01((py - ytop) / 10.0f));
                    float te = sqrtf(((px - 16.0f) / 7.4f) * ((px - 16.0f) / 7.4f) + ((py - 22.0f) / 5.2f) * ((py - 22.0f) / 5.2f));
                    float tc = c01((1.0f - te) * 5.0f + 0.5f);
                    if (tc > 0) col = mixc(col, mixc(pink, PG_RGB24(0xFF6F96), c01((py - 17.0f) / 9.0f)), tc);
                    put(c, x, y, col, cov);
                }
            break;
        default:
            for (int y = 0; y < 32; y++)
                for (int x = 0; x < 32; x++) {
                    float dx = ((float)x + 0.5f - 16.0f) / 15.0f, dy = ((float)y + 0.5f - 16.0f) / 10.0f;
                    float d = sqrtf(dx * dx + dy * dy), a = d >= 1 ? 0 : powf(1.0f - d, 1.1f) * 0.70f;
                    c->px[y * 32 + x] = PG_ARGB((uint32_t)(a * 255.0f), 255, 112, 150);
                }
            break;
        }
        blit(sheet, c, (cell % 4) * 32, (cell / 4) * 32);
        pg_img_free(c);
    }
    return upload(sheet, false);
}

/* ---------------------------------------------------------------------------- fx */
static float heart_cov(float x, float y)        /* x,y in -1.3..1.3, y up; 1 inside the heart */
{
    float a = x * x + y * y - 1.0f;
    return a * a * a - x * x * y * y * y < 0 ? 1.0f : 0.0f;
}

static PG_Tex paint_fx(void)
{
    PG_Image *sheet = pg_img_new(128, 64, 0x00FFFFFFu);
    for (int cell = 0; cell < 8; cell++) {
        PG_Image *c = pg_img_new(32, 32, 0x00FFFFFFu);
        switch (cell) {
        case FX_BEAN:
            ell(c, 16, 16, 14.0f, 4.6f, -0.62f, WHITE, with_a(0xFFFFFF, 190));
            break;
        case FX_DOT:
            for (int y = 0; y < 32; y++)
                for (int x = 0; x < 32; x++) {
                    float dx = ((float)x + 0.5f - 16.0f) / 15.0f, dy = ((float)y + 0.5f - 16.0f) / 15.0f, d = sqrtf(dx * dx + dy * dy);
                    float a = c01((1.0f - d) * 4.0f) * 0.55f + (d < 0.38f ? 0.45f : 0.0f);
                    c->px[y * 32 + x] = PG_ARGB((uint32_t)(c01(a) * 255.0f), 255, 255, 255);
                }
            break;
        case FX_HEART:
            for (int y = 0; y < 32; y++)
                for (int x = 0; x < 32; x++) {
                    float cov = 0;
                    for (int sy = 0; sy < 3; sy++) for (int sx = 0; sx < 3; sx++) {
                        float hx = (((float)x + ((float)sx + 0.5f) / 3.0f) - 16.0f) / 11.6f, hy = -(((float)y + ((float)sy + 0.5f) / 3.0f) - 16.5f) / 11.6f + 0.15f;
                        cov += heart_cov(hx, hy);
                    }
                    cov /= 9.0f;
                    if (cov > 0) put(c, x, y, mixc(PG_RGB24(0xFF8FB4), PG_RGB24(0xE63B78), c01(((float)y - 6.0f) / 22.0f)), cov);
                }
            pg_img_disc(c, 10.5f, 10.0f, 2.3f, with_a(0xFFFFFF, 210));
            break;
        case FX_SPARKLE:
            for (int y = 0; y < 32; y++)
                for (int x = 0; x < 32; x++) {
                    float dx = fabsf(((float)x + 0.5f - 16.0f) / 15.0f), dy = fabsf(((float)y + 0.5f - 16.0f) / 15.0f);
                    float s = sqrtf(dx) + sqrtf(dy);              /* astroid: a four-point star */
                    float a = c01((1.0f - s) * 3.5f);
                    float glow = c01(1.0f - sqrtf(dx * dx + dy * dy) * 1.6f);
                    c->px[y * 32 + x] = PG_ARGB((uint32_t)(c01(fmaxf(a, glow * glow * 0.8f)) * 255.0f), 255, 255, 255);
                }
            break;
        case FX_PUFF:
        case FX_SHADOW:
            for (int y = 0; y < 32; y++)
                for (int x = 0; x < 32; x++) {
                    float dx = ((float)x + 0.5f - 16.0f) / 16.0f, dy = ((float)y + 0.5f - 16.0f) / 16.0f, d = sqrtf(dx * dx + dy * dy);
                    float a = d >= 1 ? 0 : (cell == FX_PUFF ? powf(1.0f - d, 1.2f) : (1.0f - d) * (1.0f - d) * (3.0f - 2.0f * (1.0f - d)));
                    c->px[y * 32 + x] = PG_ARGB((uint32_t)(c01(a) * 255.0f), 255, 255, 255);
                }
            break;
        case FX_BALLOON: {                                        /* a little hot-air balloon for the window */
            for (int y = 0; y < 32; y++)
                for (int x = 0; x < 32; x++) {
                    float dx = ((float)x + 0.5f - 16.0f) / 11.0f, dy = ((float)y + 0.5f - 12.0f) / 11.5f;
                    float d = sqrtf(dx * dx + dy * dy);
                    if (dy > 0.55f) d += (dy - 0.55f) * 1.6f;     /* tapers toward the basket */
                    float cov = c01((1.0f - d) * 11.0f + 0.5f);
                    if (cov <= 0) continue;
                    int stripe = (int)floorf(((float)x - 5.0f) / 3.4f) & 1;
                    uint32_t col = stripe ? PG_RGB24(0xFFF4E0) : PG_RGB24(0xFF5C7A);
                    col = mixc(col, PG_RGB24(0x7A3A8C), c01((dx + 0.2f) * 0.35f + dy * 0.12f));
                    put(c, x, y, col, cov);
                }
            pg_img_line(c, 12.0f, 23.0f, 13.5f, 26.0f, 0.9f, PG_RGB24(0x8A5A44));
            pg_img_line(c, 20.0f, 23.0f, 18.5f, 26.0f, 0.9f, PG_RGB24(0x8A5A44));
            pg_img_rect(c, 13, 26, 6, 4, PG_RGB24(0xC98A4B));
            break;
        }
        default:                                                  /* FX_CLOUD */
            ell(c, 16, 21, 15.0f, 7.0f, 0, with_a(0xFFFFFF, 255), with_a(0xDCEEFF, 255));
            ell(c, 11, 15, 8.0f, 7.5f, 0, WHITE, with_a(0xF2F8FF, 255));
            ell(c, 20, 13, 9.0f, 8.5f, 0, WHITE, with_a(0xF2F8FF, 255));
            break;
        }
        blit(sheet, c, (cell % 4) * 32, (cell / 4) * 32);
        pg_img_free(c);
    }
    return upload(sheet, false);
}

/* ---------------------------------------------------------------- little accessories */
static void leaf(PG_Image *c, float bx, float by, float len, float wid, float ang, uint32_t top, uint32_t bot)
{
    /* a pointed leaf growing from (bx,by) in direction ang (0 = up) */
    float ux = sinf(ang), uy = -cosf(ang);
    float cx = bx + ux * len * 0.5f, cy = by + uy * len * 0.5f;
    ell(c, cx, cy, wid, len * 0.5f, ang, top, bot);
    pg_img_line(c, bx + ux * 1.5f, by + uy * 1.5f, bx + ux * len * 0.85f, by + uy * len * 0.85f, 0.9f, with_a(0xFFFFFF, 90));
}

static PG_Tex paint_acc(void)
{
    const uint32_t g1 = PG_RGB24(0x7BE05F), g2 = PG_RGB24(0x2FA14A), stem = PG_RGB24(0x8B5A3C);
    PG_Image *sheet = pg_img_new(96, 64, 0x00FFFFFFu);
    for (int cell = 0; cell < 6; cell++) {
        PG_Image *c = pg_img_new(32, 32, 0x00FFFFFFu);
        switch (cell) {
        case 0:                                                    /* strawberry: a leafy crown */
            leaf(c, 16, 28, 17, 4.6f, -0.95f, g1, g2); leaf(c, 16, 28, 17, 4.6f, 0.95f, g1, g2); leaf(c, 16, 28, 21, 5.0f, 0.0f, g1, g2);
            leaf(c, 16, 28, 14, 4.0f, -0.45f, g1, g2); leaf(c, 16, 28, 14, 4.0f, 0.45f, g1, g2);
            break;
        case 1:                                                    /* lime: a sprout with two leaves */
            curve(c, 16, 30, 15, 20, 17, 12, 2.4f, PG_RGB24(0x3D9A3C));
            leaf(c, 17, 14, 13, 4.4f, 0.9f, g1, g2); leaf(c, 16.5f, 18, 12, 4.0f, -1.0f, g1, g2);
            break;
        case 2:                                                    /* grape: stem, leaf and a curly tendril */
            curve(c, 16, 30, 14, 20, 13, 11, 2.8f, stem);
            leaf(c, 14, 15, 15, 5.4f, 1.0f, g1, g2);
            curve(c, 13, 12, 8, 6, 6, 11, 1.5f, PG_RGB24(0x58B847));
            pg_img_disc(c, 6.0f, 11.5f, 1.2f, PG_RGB24(0x58B847));
            break;
        case 3:                                                    /* lemon: short stem and a leaf */
            curve(c, 16, 30, 16, 22, 16, 17, 3.0f, PG_RGB24(0x6E8F2E));
            leaf(c, 16, 20, 15, 5.0f, 0.8f, PG_RGB24(0xA6E85A), PG_RGB24(0x4E9E2F));
            break;
        case 4:                                                    /* sky: a star on a bendy antenna */
            curve(c, 16, 30, 11, 21, 17, 11, 1.8f, PG_RGB24(0xFFFFFF));
            pg_img_star(c, 17.0f, 9.0f, 7.5f, 3.4f, 5, 0.1f, PG_RGB24(0xFFE45E));
            pg_img_disc(c, 15.0f, 7.5f, 1.6f, with_a(0xFFFFFF, 220));
            break;
        default:                                                   /* orange: stem and two leaves */
            curve(c, 16, 30, 16, 22, 16, 17, 3.0f, stem);
            leaf(c, 16, 20, 14, 4.6f, -0.85f, g1, g2); leaf(c, 16, 20, 14, 4.6f, 0.85f, g1, g2);
            break;
        }
        blit(sheet, c, (cell % 3) * 32, (cell / 3) * 32);
        pg_img_free(c);
    }
    return upload(sheet, false);
}

/* -------------------------------------------------------------------- hint hand */
static PG_Tex paint_hand(void)
{
    /* a cartoon glove, index finger up; the fingertip is at texel (26, 6) */
    const uint32_t glove = PG_RGB24(0xFFFFFF), shade = PG_RGB24(0xC9B6EE), cuff = PG_RGB24(0xFF7FA8), cuff2 = PG_RGB24(0xFFB9D0), cuff3 = PG_RGB24(0xD9568C);
    PG_Image *im = pg_img_new(64, 64, 0x00FFFFFFu);
    for (int pass = 0; pass < 2; pass++) {               /* pass 0: soft lilac edge (grown + offset), pass 1: the white glove */
        float o = pass == 0 ? 1.4f : 0.0f, r = pass == 0 ? 1.5f : 0.0f;
        uint32_t c = pass == 0 ? shade : glove;
        ell(im, 32.0f + o, 42.0f + o, 16.0f + r, 13.5f + r, 0, c, c);                           /* palm */
        pg_img_line(im, 26.0f + o, 35.0f + o, 26.0f + o, 11.0f + o, 10.5f + 2 * r, c);         /* index finger */
        pg_img_disc(im, 26.0f + o, 11.0f + o, 5.25f + r, c);
        pg_img_disc(im, 38.5f + o, 31.0f + o, 6.2f + r, c);                                    /* curled fingers */
        pg_img_disc(im, 45.0f + o, 36.5f + o, 5.9f + r, c);
        pg_img_disc(im, 47.0f + o, 44.0f + o, 5.3f + r, c);
        pg_img_line(im, 17.0f + o, 46.0f + o, 11.5f + o, 37.0f + o, 8.0f + 2 * r, c);          /* thumb */
        pg_img_disc(im, 11.5f + o, 37.0f + o, 4.0f + r, c);
    }
    pg_img_ring(im, 38.5f, 31.0f, 6.0f, 1.0f, with_a(0xC9B6EE, 150));                            /* finger creases */
    pg_img_ring(im, 45.0f, 36.5f, 5.7f, 1.0f, with_a(0xC9B6EE, 150));
    ell(im, 33.0f, 48.0f, 11.0f, 6.5f, 0, with_a(0xC9B6EE, 0), with_a(0xC9B6EE, 110));          /* palm shading */
    pg_img_rect(im, 16, 51, 33, 13, cuff);                                                      /* the cuff */
    pg_img_rect(im, 16, 51, 33, 3, cuff2);
    pg_img_rect(im, 16, 60, 33, 4, cuff3);
    pg_img_disc(im, 24.0f, 57.0f, 1.7f, with_a(0xFFFFFF, 230)); pg_img_disc(im, 32.5f, 57.0f, 1.7f, with_a(0xFFFFFF, 230)); pg_img_disc(im, 41.0f, 57.0f, 1.7f, with_a(0xFFFFFF, 230));
    return upload(im, false);
}

/* ------------------------------------------------------------------------ room */
static PG_Tex paint_wall(void)
{
    PG_Image *im = pg_img_new(64, 64, PG_RGB24(0xFFF0D2));
    pg_img_gradient_v(im, 0, 0, 64, 64, PG_RGB24(0xFFF4DB), PG_RGB24(0xFFEACB));
    pg_img_disc(im, 16, 16, 6.2f, PG_RGB24(0xFFB9D0)); pg_img_disc(im, 48, 48, 6.2f, PG_RGB24(0xFFB9D0));
    pg_img_disc(im, 48, 16, 4.6f, PG_RGB24(0xB4E6D6)); pg_img_disc(im, 16, 48, 4.6f, PG_RGB24(0xB4E6D6));
    pg_img_disc(im, 16, 16, 2.4f, PG_RGB24(0xFFD3E2)); pg_img_disc(im, 48, 48, 2.4f, PG_RGB24(0xFFD3E2));
    pg_img_star(im, 32, 33, 6.0f, 2.8f, 5, 0.2f, PG_RGB24(0xFFD66B));
    pg_img_star(im, 0, 0, 5, 2.4f, 5, 0.3f, PG_RGB24(0xFFD66B)); pg_img_star(im, 64, 0, 5, 2.4f, 5, 0.1f, PG_RGB24(0xFFD66B));
    pg_img_star(im, 0, 64, 5, 2.4f, 5, 0.5f, PG_RGB24(0xFFD66B)); pg_img_star(im, 64, 64, 5, 2.4f, 5, 0.2f, PG_RGB24(0xFFD66B));
    pg_img_disc(im, 32, 4, 2.0f, PG_RGB24(0xCDB8F5)); pg_img_disc(im, 32, 60, 2.0f, PG_RGB24(0xCDB8F5));
    pg_img_disc(im, 4, 32, 2.0f, PG_RGB24(0xCDB8F5)); pg_img_disc(im, 60, 32, 2.0f, PG_RGB24(0xCDB8F5));
    pg_img_noise(im, 4, 5);
    return upload(im, true);
}

static PG_Tex paint_wain(void)
{
    PG_Image *im = pg_img_new(64, 64, PG_RGB24(0xCFE9FF));
    for (int i = 0; i < 4; i++) pg_img_rect(im, i * 16 + 8, 0, 8, 64, PG_RGB24(0xE6F4FF));
    for (int i = 0; i < 4; i++) pg_img_rect(im, i * 16 + 7, 0, 1, 64, PG_RGB24(0xBBDCF7));
    pg_img_noise(im, 4, 9);
    return upload(im, true);
}

static PG_Tex paint_ceil(void)
{
    PG_Image *im = pg_img_new(64, 64, PG_RGB24(0xD6E3FF));
    pg_img_gradient_v(im, 0, 0, 64, 64, PG_RGB24(0xE2EBFF), PG_RGB24(0xD0DFFF));
    for (int i = 0; i < 4; i++) pg_img_star(im, 8.0f + 16.0f * (float)i, 16.0f + 32.0f * (float)(i & 1), 5.0f, 2.2f, 5, 0.3f * (float)i, WHITE);
    pg_img_noise(im, 4, 11);
    return upload(im, true);
}

static PG_Tex paint_floor(void)
{
    PG_Image *im = pg_img_new(64, 64, WHITE);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            float dx = ((float)x + 0.5f - 32.0f) / 32.0f, dy = ((float)y + 0.5f - 32.0f) / 32.0f;
            float m = fmaxf(fabsf(dx), fabsf(dy));
            float k = 1.0f - 0.10f * sm(0.45f, 1.0f, m) + 0.04f * (1.0f - m) * (1.0f - m);
            if (k > 1.0f) k = 1.0f;
            im->px[y * 64 + x] = pg_argb_scale(WHITE, k);
        }
    pg_img_frame(im, 0, 0, 64, 64, 2, PG_RGB24(0xB7C3DA));                       /* seam between foam tiles */
    pg_img_frame(im, 2, 2, 60, 60, 1, PG_RGB24(0xFFFFFF));                      /* lit bevel */
    pg_img_rect(im, 3, 60, 58, 1, PG_RGB24(0xDCE3F0)); pg_img_rect(im, 60, 3, 1, 58, PG_RGB24(0xDCE3F0));
    pg_img_star(im, 33.0f, 34.0f, 15.0f, 7.0f, 5, 0.0f, PG_RGB24(0xE2E8F4));    /* embossed star: shadow ... */
    pg_img_star(im, 32.0f, 33.0f, 15.0f, 7.0f, 5, 0.0f, PG_RGB24(0xFFFFFF));    /* ... and light */
    pg_img_noise(im, 7, 13);
    return upload(im, true);
}

static PG_Tex paint_wood(void)
{
    PG_Image *im = pg_img_new(32, 32, PG_RGB24(0xFFF4E6));
    pg_img_gradient_v(im, 0, 0, 32, 32, PG_RGB24(0xFFFFFF), PG_RGB24(0xF2E2D0));
    for (int y = 3; y < 32; y += 6) pg_img_rect(im, 0, y, 32, 1, with_a(0xC9A27A, 90));
    pg_img_rect(im, 0, 0, 32, 1, PG_RGB24(0xFFFFFF));
    pg_img_rect(im, 0, 31, 32, 1, PG_RGB24(0xD7C0A6));
    pg_img_noise(im, 5, 21);
    return upload(im, true);
}

static PG_Tex paint_sky(void)
{
    PG_Image *im = pg_img_new(64, 64, PG_RGB24(0x7CC8FF));
    pg_img_gradient_v(im, 0, 0, 64, 64, PG_RGB24(0x6EBFFF), PG_RGB24(0xDDF4FF));
    pg_img_gradient_radial(im, 48, 15, 20, with_a(0xFFF6BE, 255), with_a(0xFFF6BE, 255));
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            float dx = (float)x + 0.5f - 48.0f, dy = (float)y + 0.5f - 15.0f, d = sqrtf(dx * dx + dy * dy);
            if (d < 20.0f) im->px[y * 64 + x] = mixc(im->px[y * 64 + x], PG_RGB24(0xFFF3B0), (1.0f - d / 20.0f) * 0.75f);
        }
    pg_img_disc(im, 48, 15, 6.5f, PG_RGB24(0xFFF7C8));
    pg_img_noise(im, 3, 31);
    return upload(im, true);
}

static PG_Tex paint_flag(void)
{
    PG_Image *im = pg_img_new(32, 32, PG_RGB24(0xFFFFFF));
    pg_img_gradient_v(im, 0, 0, 32, 32, PG_RGB24(0xFFFFFF), PG_RGB24(0xE9E4F4));
    for (int i = 0; i < 6; i++) pg_img_disc(im, 7.0f + 9.0f * (float)(i % 3) + ((i / 3) ? 4.0f : 0.0f), 9.0f + 9.0f * (float)(i / 3), 2.0f, PG_RGB24(0xD8D0EE));
    pg_img_noise(im, 3, 41);
    return upload(im, true);
}

static PG_Tex paint_fur(void)
{
    PG_Image *im = pg_img_new(32, 32, PG_RGB24(0xF4F0EC));
    for (int y = 0; y < 32; y += 2) pg_img_rect(im, 0, y, 32, 1, with_a(0xB8AEA6, 60));
    pg_img_noise(im, 14, 51);
    return upload(im, true);
}

static PG_Tex paint_block(char letter, uint32_t body, uint32_t ink)
{
    PG_Image *im = pg_img_new(32, 32, PG_RGB24(body));
    pg_img_gradient_v(im, 0, 0, 32, 32, mixc(PG_RGB24(body), WHITE, 0.25f), mixc(PG_RGB24(body), PG_RGB24(0x40205A), 0.12f));
    pg_img_frame(im, 0, 0, 32, 32, 2, mixc(PG_RGB24(body), PG_RGB24(0x40205A), 0.25f));
    char s[2] = { letter, 0 };
    pg_img_text(im, 9, 6, 3, PG_RGB24(ink), s);
    pg_img_noise(im, 4, 61u + (uint32_t)letter);
    return upload(im, true);
}

static PG_Tex paint_ball(void)
{
    static const uint32_t col[6] = { 0xFF6B8B, 0xFFFFFF, 0x4DABF7, 0xFFFFFF, 0xFFD43B, 0xFFFFFF };
    PG_Image *im = pg_img_new(32, 32, WHITE);
    for (int i = 0; i < 6; i++) pg_img_rect(im, i * 32 / 6, 0, 32 / 6 + 1, 32, PG_RGB24(col[i]));
    return upload(im, true);
}

void art_init(Art *a)
{
    for (int i = 0; i < JB_STYLES; i++) a->jelly[i] = paint_jelly(CANDY[i], (uint32_t)(i * 7 + 3));
    a->face = paint_face();
    a->fx = paint_fx();
    a->hand = paint_hand();
    a->acc = paint_acc();
    a->wall = paint_wall(); a->wain = paint_wain(); a->ceil_t = paint_ceil(); a->floor_t = paint_floor();
    a->wood = paint_wood(); a->sky = paint_sky(); a->flag = paint_flag(); a->fur = paint_fur();
    a->block[0] = paint_block('A', 0xFF8FA6, 0xFFFFFF);
    a->block[1] = paint_block('B', 0xFFD95C, 0x8A4B00);
    a->block[2] = paint_block('C', 0x5CC4FF, 0xFFFFFF);
    a->ball = paint_ball();
}
