/* paint.c - every texture in the game is painted here at init, from nothing (no files).
 * Each painter returns a PG_Image; the caller uploads it with tex_upload() (which applies the PS1 15-bit
 * dither unless told otherwise).  Sizes are small on purpose: chunky texels are the style. */
#include "bp.h"

#define C24(x) PG_RGB24(x)

static inline uint32_t mixc(uint32_t a, uint32_t b, float t) { return pg_argb_lerp(a, b, t); }
static inline float sstep(float e0, float e1, float x) { return smooth01((x - e0) / (e1 - e0)); }

/* write straight alpha over what is there (anti-aliased coverage already folded into `a` 0..1) */
static void plot(PG_Image *im, int x, int y, uint32_t rgb, float a)
{
    if (x < 0 || y < 0 || x >= im->w || y >= im->h || a <= 0.0f) return;
    uint32_t c = (rgb & 0xFFFFFFu) | ((uint32_t)(clampf(a, 0, 1) * 255.0f + 0.5f) << 24);
    pg_img_rect(im, x, y, 1, 1, c);
}

/* ------------------------------------------------------------------ balloon */
PG_Image *paint_balloon(void)
{
    /* grey ramp: the balloon colour is the tint, this adds a lighter shoulder and a deeper belly near the knot */
    PG_Image *im = pg_img_new(32, 32, 0xFFFFFFFFu);
    for (int j = 0; j < 32; j++) {
        float v = (float)j / 31.0f;
        float k = v < 0.35f ? 1.0f - 0.04f * (v / 0.35f) : 0.96f - 0.30f * sstep(0.35f, 1.0f, v);
        uint8_t g = (uint8_t)(clampf(k, 0, 1) * 255.0f);
        pg_img_rect(im, 0, j, 32, 1, PG_ARGB(255, g, g, g));
    }
    return im;
}

/* a bean-shaped window reflection plus a tiny second glint, white with soft alpha */
PG_Image *paint_glint(void)
{
    PG_Image *im = pg_img_new(32, 32, 0x00FFFFFFu);
    const float ang = -0.72f, ca = cosf(ang), sa = sinf(ang);
    for (int j = 0; j < 32; j++)
        for (int i = 0; i < 32; i++) {
            float x = (float)i + 0.5f - 13.0f, y = (float)j + 0.5f - 12.0f;
            float u = x * ca + y * sa, v = -x * sa + y * ca;
            float q = (u * u) / (11.5f * 11.5f) + (v * v) / (4.4f * 4.4f);
            float a = clampf((1.0f - q) * 2.4f, 0, 1);
            float dx = (float)i + 0.5f - 24.5f, dy = (float)j + 0.5f - 22.5f;
            float a2 = clampf(2.9f - sqrtf(dx * dx + dy * dy), 0, 1) * 0.9f;
            if (a2 > a) a = a2;
            im->px[j * 32 + i] = PG_ARGB((uint32_t)(a * 255.0f), 255, 255, 255);
        }
    return im;
}

/* four-point twinkle with a soft core */
PG_Image *paint_sparkle(void)
{
    PG_Image *im = pg_img_new(64, 64, 0x00FFFFFFu);
    pg_img_star(im, 32, 32, 31, 5.0f, 4, 0, 0xFFFFFFFFu);
    for (int j = 0; j < 64; j++)
        for (int i = 0; i < 64; i++) {
            float dx = (float)i + 0.5f - 32.0f, dy = (float)j + 0.5f - 32.0f;
            float g = clampf(1.0f - sqrtf(dx * dx + dy * dy) / 15.0f, 0, 1);
            g = g * g * 0.95f;
            uint32_t c = im->px[j * 64 + i];
            float a = (float)(c >> 24) / 255.0f;
            a = 1.0f - (1.0f - a) * (1.0f - g);
            im->px[j * 64 + i] = PG_ARGB((uint32_t)(a * 255.0f), 255, 255, 255);
        }
    return im;
}

PG_Image *paint_ring(void)
{
    PG_Image *im = pg_img_new(64, 64, 0x00FFFFFFu);
    for (int j = 0; j < 64; j++)
        for (int i = 0; i < 64; i++) {
            float dx = (float)i + 0.5f - 32.0f, dy = (float)j + 0.5f - 32.0f;
            float d = sqrtf(dx * dx + dy * dy);
            float a = clampf(1.0f - fabsf(d - 25.5f) / 5.5f, 0, 1);
            a = a * a * (3.0f - 2.0f * a);
            im->px[j * 64 + i] = PG_ARGB((uint32_t)(a * 255.0f), 255, 255, 255);
        }
    return im;
}

/* ------------------------------------------------------------------- room */
/* wallpaper tile (repeats): pale mint with soft stripes, white dots and tiny pink dots */
PG_Image *paint_wall(void)
{
    PG_Image *im = pg_img_new(64, 64, C24(0xBDE8D5));
    for (int x = 0; x < 64; x += 32) pg_img_rect(im, x, 0, 16, 64, PG_ARGB(60, 255, 255, 255));
    pg_img_disc(im, 16, 16, 7.0f, PG_ARGB(235, 255, 255, 255));
    pg_img_disc(im, 48, 48, 7.0f, PG_ARGB(235, 255, 255, 255));
    pg_img_disc(im, 48, 16, 3.2f, C24(0xFFB4CC));
    pg_img_disc(im, 16, 48, 3.2f, C24(0xFFD870));
    pg_img_noise(im, 3, 11);
    return im;
}

/* crown moulding: cream strip with a scalloped, multi-coloured edge */
PG_Image *paint_trim(void)
{
    PG_Image *im = pg_img_new(128, 16, C24(0xFFF4DC));
    pg_img_gradient_v(im, 0, 0, 128, 10, C24(0xFFFFFF), C24(0xFFEBC4));
    static const uint32_t sc[4] = { 0xFF7BAF, 0xFFD13A, 0x55AEFF, 0x56D68A };
    for (int k = 0; k < 8; k++) pg_img_disc(im, 8.0f + 16.0f * (float)k, 6.5f, 8.0f, C24(sc[k & 3]));
    pg_img_rect(im, 0, 0, 128, 3, C24(0xFFFFFF));
    pg_img_noise(im, 3, 5);
    return im;
}

/* wainscot panels: pink stripes with a slightly darker groove */
PG_Image *paint_wainscot(void)
{
    PG_Image *im = pg_img_new(64, 32, C24(0xFFB8CE));
    for (int x = 0; x < 64; x += 16) pg_img_rect(im, x, 0, 8, 32, C24(0xFFCDDD));
    for (int x = 7; x < 64; x += 16) pg_img_rect(im, x, 0, 2, 32, C24(0xF59AB8));
    pg_img_noise(im, 3, 17);
    return im;
}

/* honey wood boards running away from the viewer */
PG_Image *paint_floor(void)
{
    PG_Image *im = pg_img_new(64, 64, C24(0xE3AE6E));
    static const uint32_t tone[4] = { 0xE6B374, 0xDCA562, 0xEBBB7E, 0xE0AA68 };
    for (int b = 0; b < 4; b++) {
        pg_img_rect(im, b * 16, 0, 16, 64, C24(tone[b]));
        for (int g = 0; g < 5; g++) pg_img_rect(im, b * 16 + 2 + g * 3, 0, 1, 64, PG_ARGB(34, 120, 70, 20));
        pg_img_rect(im, b * 16, 0, 1, 64, C24(0xB77D3E));
        int cut = (b * 37 + 19) % 56 + 4;
        pg_img_rect(im, b * 16, cut, 16, 1, C24(0xB77D3E));
    }
    pg_img_noise(im, 5, 23);
    return im;
}

/* round-ish rug, mapped planar onto an ellipse: concentric rings, a star in the middle */
PG_Image *paint_rug(void)
{
    PG_Image *im = pg_img_new(64, 64, C24(0xF0577F));
    for (int j = 0; j < 64; j++)
        for (int i = 0; i < 64; i++) {
            float dx = ((float)i + 0.5f - 32.0f) / 32.0f, dy = ((float)j + 0.5f - 32.0f) / 32.0f;
            float d = sqrtf(dx * dx + dy * dy);
            uint32_t c;
            if (d < 0.20f)       c = 0xFFD54A;
            else if (d < 0.30f)  c = 0xF0577F;
            else if (d < 0.38f)  c = 0xFFF1D2;
            else if (d < 0.60f)  c = 0x2FBDB4;
            else if (d < 0.68f)  c = 0xFFF1D2;
            else if (d < 0.90f)  c = (((i / 4) + (j / 4)) & 1) ? 0xF0577F : 0xF4739A;
            else                 c = 0xFFE9B8;
            im->px[j * 64 + i] = C24(c);
        }
    pg_img_star(im, 32, 32, 10, 4.5f, 5, 0.0f, C24(0xFF9F2E));
    pg_img_noise(im, 6, 31);
    return im;
}

static void cloud(PG_Image *im, float cx, float cy, float s)
{
    static const float d[5][3] = { { -8, 3, 6.5f }, { -1, -2, 8.5f }, { 8, 1, 7 }, { 15, 4, 5 }, { 3, 4, 7 } };
    for (int pass = 0; pass < 2; pass++)
        for (int k = 0; k < 5; k++)
            pg_img_disc(im, cx + d[k][0] * s, cy + d[k][1] * s + (pass == 0 ? 1.6f : 0.0f), d[k][2] * s, pass == 0 ? C24(0xD7E8FA) : C24(0xFFFFFF));
}

/* the view outside: 0 = sunny hills with a sun, 1 = clouds and a rainbow */
PG_Image *paint_window(int which)
{
    PG_Image *im = pg_img_new(96, 128, 0xFF000000u);
    pg_img_gradient_v(im, 0, 0, 96, 128, C24(0x3F9EFF), C24(0xC9EAFF));
    if (which == 0) {
        pg_img_star(im, 30, 30, 26, 14, 12, 0.1f, PG_ARGB(120, 255, 236, 120));
        pg_img_disc(im, 30, 30, 14, C24(0xFFE86A));
        pg_img_disc(im, 28, 28, 8.5f, C24(0xFFF7B8));
        cloud(im, 64, 24, 1.1f);
        cloud(im, 40, 70, 0.8f);
    } else {
        for (int k = 0; k < 6; k++) {
            static const uint32_t rb[6] = { 0xFF5A5A, 0xFFA83A, 0xFFE84A, 0x56D66E, 0x4AA8FF, 0xA070FF };
            pg_img_ring(im, 48, 118, 70.0f - 4.4f * (float)k, 4.6f, C24(rb[k]));
        }
        cloud(im, 22, 26, 1.0f);
        cloud(im, 72, 40, 0.75f);
        cloud(im, 70, 78, 0.9f);
    }
    pg_img_disc(im, 16, 150, 58, C24(0x7BDC83));
    pg_img_disc(im, 84, 154, 60, C24(0x55C26E));
    pg_img_disc(im, 52, 170, 56, C24(0x8FE48B));
    for (int k = 0; k < 4; k++) {
        float tx = 18.0f + 22.0f * (float)k, ty = 108.0f + 6.0f * (float)(k & 1);
        pg_img_rect(im, (int)tx, (int)ty, 2, 6, C24(0x8A5A32));
        pg_img_disc(im, tx + 1.0f, ty - 1.0f, 5.0f, C24(0x2F9E54));
    }
    /* a faint glass streak */
    pg_img_line(im, 8, 126, 40, 6, 7, PG_ARGB(26, 255, 255, 255));
    return im;
}

PG_Image *paint_frame(void)
{
    PG_Image *im = pg_img_new(16, 16, C24(0xFFFBF2));
    pg_img_rect(im, 0, 0, 16, 2, C24(0xFFFFFF));
    pg_img_rect(im, 0, 14, 16, 2, C24(0xEADFCB));
    pg_img_noise(im, 3, 9);
    return im;
}

/* wrapping paper (dots / stripes / stars) with a ribbon cross; every face of the box gets the same picture */
PG_Image *paint_gift(uint32_t paper, uint32_t ribbon, int style)
{
    PG_Image *im = pg_img_new(32, 32, C24(paper));
    uint32_t light = mixc(C24(paper), 0xFFFFFFFFu, 0.38f);
    if (style == 0) {
        for (int j = 0; j < 4; j++)
            for (int i = 0; i < 4; i++) pg_img_disc(im, 4.0f + 8.0f * (float)i + ((j & 1) ? 4.0f : 0.0f), 4.0f + 8.0f * (float)j, 1.8f, light);
    } else if (style == 1) {
        for (int x = 0; x < 32; x += 8) pg_img_rect(im, x, 0, 4, 32, light);
    } else {
        for (int j = 0; j < 3; j++)
            for (int i = 0; i < 3; i++) pg_img_star(im, 5.0f + 11.0f * (float)i + ((j & 1) ? 5.0f : 0.0f), 5.0f + 11.0f * (float)j, 3.6f, 1.6f, 5, 0.0f, light);
    }
    uint32_t rd = mixc(C24(ribbon), 0xFF000000u, 0.22f), rl = mixc(C24(ribbon), 0xFFFFFFFFu, 0.35f);
    pg_img_rect(im, 12, 0, 8, 32, C24(ribbon));
    pg_img_rect(im, 0, 12, 32, 8, C24(ribbon));
    pg_img_rect(im, 12, 0, 1, 32, rd); pg_img_rect(im, 19, 0, 1, 32, rd);
    pg_img_rect(im, 0, 12, 32, 1, rd); pg_img_rect(im, 0, 19, 32, 1, rd);
    pg_img_rect(im, 15, 0, 2, 32, rl); pg_img_rect(im, 0, 15, 32, 2, rl);
    pg_img_frame(im, 0, 0, 32, 32, 1, mixc(C24(paper), 0xFF000000u, 0.18f));
    pg_img_noise(im, 4, 3);
    return im;
}

/* a bow, drawn as a sprite on top of a present (transparent background) */
PG_Image *paint_bow(uint32_t ribbon)
{
    PG_Image *im = pg_img_new(32, 32, 0x00FFFFFFu);
    uint32_t dark = mixc(C24(ribbon), 0xFF000000u, 0.25f), lite = mixc(C24(ribbon), 0xFFFFFFFFu, 0.45f);
    pg_img_line(im, 16, 17, 8, 29, 5.0f, dark);
    pg_img_line(im, 16, 17, 24, 29, 5.0f, dark);
    pg_img_line(im, 16, 17, 8.5f, 28, 3.0f, C24(ribbon));
    pg_img_line(im, 16, 17, 23.5f, 28, 3.0f, C24(ribbon));
    for (int side = -1; side <= 1; side += 2) {
        float cx = 16.0f + (float)side * 7.5f;
        for (int j = 0; j < 32; j++)
            for (int i = 0; i < 32; i++) {
                float dx = ((float)i + 0.5f - cx) / 8.2f, dy = ((float)j + 0.5f - 12.5f) / 7.0f;
                float q = sqrtf(dx * dx + dy * dy);
                float ao = clampf((1.0f - q) * 8.0f, 0, 1);
                float ai = clampf((q - 0.5f) * 8.0f, 0, 1);
                float a = ao * ai;
                if (a > 0) plot(im, i, j, j < 12 ? lite : C24(ribbon), a);
            }
    }
    pg_img_disc(im, 16, 14, 4.4f, dark);
    pg_img_disc(im, 15.4f, 13.4f, 3.0f, C24(ribbon));
    return im;
}

/* eight pennant patterns in a 4x2 sheet of 32x32 cells */
PG_Image *paint_flags(void)
{
    PG_Image *im = pg_img_new(128, 64, 0xFFFFFFFFu);
    static const uint32_t base[8] = { 0xFF7EB6, 0xFFD83A, 0x57A8FF, 0xFF8A4A, 0x58D68D, 0xA88BFF, 0xFF5A5F, 0x2FD0C8 };
    static const uint32_t acc[8]  = { 0xFFFFFF, 0xFFA52E, 0xFFFFFF, 0xFFE35A, 0xFFFFFF, 0xFFE35A, 0xFFFFFF, 0xFFF1A0 };
    for (int k = 0; k < 8; k++) {
        int ox = (k % 4) * 32, oy = (k / 4) * 32;
        pg_img_rect(im, ox, oy, 32, 32, C24(base[k]));
        switch (k & 3) {
        case 0: for (int j = 0; j < 3; j++) for (int i = 0; i < 3; i++) pg_img_disc(im, (float)ox + 6.0f + 10.0f * (float)i, (float)oy + 6.0f + 10.0f * (float)j, 2.6f, C24(acc[k])); break;
        case 1: for (int y = 2; y < 32; y += 8) pg_img_rect(im, ox, oy + y, 32, 4, C24(acc[k])); break;
        case 2: for (int x = 2; x < 28; x += 8) { pg_img_line(im, (float)(ox + x), (float)oy + 5.0f, (float)(ox + x + 4), (float)oy + 12.0f, 2.2f, C24(acc[k])); pg_img_line(im, (float)(ox + x + 4), (float)oy + 12.0f, (float)(ox + x + 8), (float)oy + 5.0f, 2.2f, C24(acc[k])); } break;
        default: pg_img_star(im, (float)ox + 16.0f, (float)oy + 14.0f, 9.0f, 4.0f, 5, 0.0f, C24(acc[k])); break;
        }
        pg_img_frame(im, ox, oy, 32, 32, 1, mixc(C24(base[k]), 0xFF000000u, 0.15f));
    }
    pg_img_noise(im, 3, 41);
    return im;
}

PG_Image *paint_twine(void)
{
    PG_Image *im = pg_img_new(8, 8, C24(0xD7B48A));
    pg_img_stripes(im, 1, 3, C24(0xD7B48A), C24(0xB8946A));
    pg_img_noise(im, 6, 2);
    return im;
}

PG_Image *paint_streamer(uint32_t a, uint32_t b)
{
    PG_Image *im = pg_img_new(16, 32, C24(a));
    pg_img_stripes(im, 4, 8, C24(a), C24(b));
    pg_img_noise(im, 4, 13);
    return im;
}

PG_Image *paint_rainbow(void)
{
    PG_Image *im = pg_img_new(128, 64, 0x00FFFFFFu);
    static const uint32_t rb[6] = { 0xFF6B6B, 0xFFAA45, 0xFFE55A, 0x5ED87A, 0x55AEFF, 0xAE7BFF };
    for (int k = 0; k < 6; k++) pg_img_ring(im, 64, 66, 60.0f - 5.4f * (float)k, 5.6f, C24(rb[k]));
    return im;
}

/* ------------------------------------------------------------- the hint hand */
/* A cartoon glove pointing up-left: fingertip near (30,9). Outline in indigo, never black. */
PG_Image *paint_hand(void)
{
    PG_Image *im = pg_img_new(96, 96, 0x00FFFFFFu);
    const uint32_t ol = C24(0x5B67B5), shade = C24(0xC3CEF2), white = C24(0xFFFFFF);
    struct { int line; float a, b, c, d, t; } sh[6] = {
        { 1, 44, 52, 31, 16, 17.5f },     /* index finger */
        { 0, 54, 64, 0, 0, 25.0f },       /* palm */
        { 1, 36, 72, 25, 55, 14.5f },     /* thumb */
        { 0, 63, 45, 0, 0, 10.0f },       /* curled fingers */
        { 0, 74, 53, 0, 0, 9.6f },
        { 0, 80, 64, 0, 0, 9.0f },
    };
    /* cuff first (pink glove cuff running off the corner), so the hand overlaps it */
    pg_img_line(im, 66, 84, 84, 104, 27, ol);
    pg_img_line(im, 66, 84, 84, 104, 23, C24(0xFF6FA8));
    pg_img_line(im, 66, 84, 84, 104, 14, C24(0xFF8DBD));
    for (int pass = 0; pass < 3; pass++)
        for (int k = 0; k < 6; k++) {
            float grow = pass == 0 ? 2.6f : pass == 1 ? 0.0f : -2.8f, dx = pass == 2 ? -1.5f : 0.0f, dy = pass == 2 ? -1.9f : 0.0f;
            uint32_t col = pass == 0 ? ol : pass == 1 ? shade : white;
            if (sh[k].line) pg_img_line(im, sh[k].a + dx, sh[k].b + dy, sh[k].c + dx, sh[k].d + dy, sh[k].t + grow * 2.0f, col);
            else pg_img_disc(im, sh[k].a + dx, sh[k].b + dy, sh[k].t + grow, col);
        }
    /* finger creases between the curled knuckles, and a soft crease at the thumb */
    pg_img_line(im, 68, 41, 63, 54, 1.5f, shade);
    pg_img_line(im, 78, 49, 72, 61, 1.5f, shade);
    pg_img_line(im, 38, 60, 46, 62, 1.5f, shade);
    return im;
}

/* ---------------------------------------------------------------- prizes */
PG_Image *paint_prize(int kind)
{
    PG_Image *im = pg_img_new(32, 32, 0xFFFFFFFFu);
    uint32_t top, bot, hi;
    if (kind == 0)      { top = 0xFFF58A; bot = 0xFFB000; hi = 0xFFFFD0; }
    else if (kind == 1) { top = 0xFF9DC2; bot = 0xE5285A; hi = 0xFFD2E2; }
    else                { top = 0x9CC2FF; bot = 0x5B3FE6; hi = 0xD8E6FF; }
    pg_img_gradient_v(im, 0, 0, 32, 32, C24(top), C24(bot));
    pg_img_line(im, 2, 30, 30, 2, 6, mixc(C24(hi), C24(top), 0.5f));
    for (int k = 0; k < 6; k++) pg_img_disc(im, 4.0f + 5.0f * (float)k, 6.0f + 3.5f * (float)((k * 5) % 6), 0.9f, C24(hi));
    pg_img_noise(im, 4, 7 + (uint32_t)kind);
    return im;
}
