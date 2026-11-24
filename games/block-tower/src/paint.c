/* paint.c - Block Tower textures, painted procedurally (no files). See paint.h.
 *
 * Toy-block faces keep a 6 px "rim" band: the bevelled block mesh maps the outer ~10 % of each face texture
 * onto its chamfers, so the rim is painted a shade darker with a crisp light edge (routed, painted wood).
 * A private RNG is used so painting never disturbs the game's pg_rand() sequence. */
#include "paint.h"

#include <math.h>
#include <string.h>

#define C(hex) PG_RGB24(hex)

/* ------------------------------------------------------------------ helpers */
static uint32_t rs = 1;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static float rndf(void) { return (float)(rnd() >> 8) * (1.0f / 16777216.0f); }
static void seed(uint32_t s) { rs = s ? s : 1u; for (int i = 0; i < 4; i++) (void)rnd(); }

static inline float cl01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static uint32_t shade(uint32_t c, float k) { return pg_argb_scale(c, k); }
static uint32_t alpha(uint32_t c, int a) { return (c & 0x00FFFFFFu) | ((uint32_t)a << 24); }

/* straight-alpha "over" of colour s, scaled by coverage cov, onto pixel (x,y); wrapx tiles horizontally */
static void put(PG_Image *im, int x, int y, uint32_t s, float cov, int wrapx)
{
    if (wrapx) x = ((x % im->w) + im->w) % im->w;
    if (x < 0 || y < 0 || x >= im->w || y >= im->h) return;
    float sa = (float)(s >> 24) * (1.0f / 255.0f) * cov;
    if (sa <= 0.0f) return;
    uint32_t *d = &im->px[y * im->w + x];
    uint32_t dv = *d;
    float da = (float)(dv >> 24) * (1.0f / 255.0f);
    float oa = sa + da * (1.0f - sa);
    uint32_t out = (uint32_t)(oa * 255.0f + 0.5f) << 24;
    for (int sh = 0; sh <= 16; sh += 8) {
        float sc = (float)((s >> sh) & 255), dc = (float)((dv >> sh) & 255);
        out |= (uint32_t)((sc * sa + dc * da * (1.0f - sa)) / oa + 0.5f) << sh;
    }
    *d = out;
}

static void tri(PG_Image *im, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c)
{
    float minx = fminf(x0, fminf(x1, x2)), maxx = fmaxf(x0, fmaxf(x1, x2));
    float miny = fminf(y0, fminf(y1, y2)), maxy = fmaxf(y0, fmaxf(y1, y2));
    for (int j = (int)floorf(miny); j <= (int)ceilf(maxy); j++)
        for (int i = (int)floorf(minx); i <= (int)ceilf(maxx); i++) {
            int hit = 0;
            for (int sy = 0; sy < 3; sy++)
                for (int sx = 0; sx < 3; sx++) {
                    float px = (float)i + ((float)sx + 0.5f) / 3.0f, py = (float)j + ((float)sy + 0.5f) / 3.0f;
                    float d0 = (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0);
                    float d1 = (x2 - x1) * (py - y1) - (y2 - y1) * (px - x1);
                    float d2 = (x0 - x2) * (py - y2) - (y0 - y2) * (px - x2);
                    if ((d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0)) hit++;
                }
            if (hit) put(im, i, j, c, (float)hit / 9.0f, 0);
        }
}

static void rrect(PG_Image *im, float x, float y, float w, float h, float r, uint32_t c)
{
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h * 0.5f) r = h * 0.5f;
    float cx = x + w * 0.5f, cy = y + h * 0.5f;
    for (int j = (int)floorf(y) - 1; j <= (int)ceilf(y + h) + 1; j++)
        for (int i = (int)floorf(x) - 1; i <= (int)ceilf(x + w) + 1; i++) {
            float px = fabsf((float)i + 0.5f - cx) - (w * 0.5f - r), py = fabsf((float)j + 0.5f - cy) - (h * 0.5f - r);
            float d = hypotf(fmaxf(px, 0), fmaxf(py, 0)) + fminf(fmaxf(px, py), 0) - r;
            put(im, i, j, c, cl01(0.5f - d), 0);
        }
}

/* five-pointed star with independent x/y radii (for textures that get stretched, such as sphere skins) */
static void star_xy(PG_Image *im, float cx, float cy, float rx, float ry, uint32_t c)
{
    float vx[10], vy[10];
    for (int k = 0; k < 10; k++) {
        float a = (float)k * PG_PI / 5.0f - PG_PI * 0.5f, rr = (k & 1) ? 0.46f : 1.0f;
        vx[k] = cx + cosf(a) * rx * rr; vy[k] = cy + sinf(a) * ry * rr;
    }
    for (int k = 0; k < 10; k++) tri(im, cx, cy, vx[k], vy[k], vx[(k + 1) % 10], vy[(k + 1) % 10], c);
}

static void ellipse(PG_Image *im, float cx, float cy, float rx, float ry, uint32_t c)
{
    float m = fminf(rx, ry);
    for (int j = (int)floorf(cy - ry) - 1; j <= (int)ceilf(cy + ry) + 1; j++)
        for (int i = (int)floorf(cx - rx) - 1; i <= (int)ceilf(cx + rx) + 1; i++) {
            float dx = ((float)i + 0.5f - cx) / rx, dy = ((float)j + 0.5f - cy) / ry;
            put(im, i, j, c, cl01(0.5f - (sqrtf(dx * dx + dy * dy) - 1.0f) * m), 0);
        }
}

/* short horizontal streaks: wood grain showing through paint */
static void grain(PG_Image *im, uint32_t col, int count, int maxa, int wrapx)
{
    for (int k = 0; k < count; k++) {
        int x = (int)(rndf() * (float)im->w), y = (int)(rndf() * (float)im->h), len = 6 + (int)(rndf() * 26.0f);
        int a = 8 + (int)(rndf() * (float)(maxa - 8));
        for (int i = 0; i < len; i++) put(im, x + i, y, alpha(col, a), 1.0f, wrapx);
    }
}

/* ------------------------------------------------------------ toy block faces */
static PG_Image *face_new(int w, int h, uint32_t base, uint32_t sd)
{
    PG_Image *im = pg_img_new(w, h, base);
    seed(sd);
    pg_img_gradient_v(im, 0, 0, w, h, shade(base, 1.07f), shade(base, 0.93f));
    grain(im, shade(base, 0.72f), w * h / 70, 40, 0);
    grain(im, shade(base, 1.22f), w * h / 120, 34, 0);
    return im;
}

static PG_Image *face_done(PG_Image *im, uint32_t base, int rim)
{
    int w = im->w, h = im->h;
    pg_img_frame(im, 0, 0, w, h, 2, shade(base, 0.58f));
    pg_img_frame(im, 2, 2, w - 4, h - 4, rim - 2, shade(base, 0.80f));
    pg_img_frame(im, rim, rim, w - 2 * rim, h - 2 * rim, 1, shade(base, 1.20f));
    pg_img_noise(im, 5, 91);
    pg_img_ps1(im);
    return im;
}

/* stroke glyphs: polylines in a unit box, y down */
typedef struct { int n; float p[11][2]; } Poly;
typedef struct { int ns; Poly s[3]; } Glyph;
static const Glyph glyph_a = { 2, { { 3, { { 0.06f, 1 }, { 0.5f, 0 }, { 0.94f, 1 } } }, { 2, { { 0.25f, 0.66f }, { 0.75f, 0.66f } } } } };
static const Glyph glyph_b = { 3, { { 2, { { 0.14f, 0 }, { 0.14f, 1 } } },
    { 7, { { 0.14f, 0 }, { 0.58f, 0 }, { 0.82f, 0.1f }, { 0.9f, 0.25f }, { 0.82f, 0.42f }, { 0.58f, 0.5f }, { 0.14f, 0.5f } } },
    { 6, { { 0.58f, 0.5f }, { 0.88f, 0.6f }, { 0.97f, 0.76f }, { 0.88f, 0.92f }, { 0.6f, 1 }, { 0.14f, 1 } } } } };
static const Glyph glyph_1 = { 2, { { 3, { { 0.2f, 0.22f }, { 0.55f, 0 }, { 0.55f, 1 } } }, { 2, { { 0.2f, 1 }, { 0.9f, 1 } } } } };

static void glyph_draw(PG_Image *im, const Glyph *g, float cx, float cy, float gw, float gh, float th, uint32_t col)
{
    for (int s = 0; s < g->ns; s++)
        for (int k = 0; k + 1 < g->s[s].n; k++)
            pg_img_line(im, cx - gw * 0.5f + g->s[s].p[k][0] * gw, cy - gh * 0.5f + g->s[s].p[k][1] * gh,
                        cx - gw * 0.5f + g->s[s].p[k + 1][0] * gw, cy - gh * 0.5f + g->s[s].p[k + 1][1] * gh, th, col);
}

static void glyph_decal(PG_Image *im, const Glyph *g, uint32_t base, uint32_t col)
{
    glyph_draw(im, g, 32.5f, 33.5f, 27, 34, 8.0f, shade(base, 0.5f));
    glyph_draw(im, g, 30, 31, 27, 34, 8.0f, col);
    glyph_draw(im, g, 29.2f, 30.2f, 27, 34, 2.4f, alpha(C(0xFFFFFF), 120));
}

static void star_decal(PG_Image *im, float cx, float cy, float r, uint32_t base, uint32_t col)
{
    pg_img_star(im, cx + 2, cy + 3, r, r * 0.47f, 5, 0, shade(base, 0.5f));
    pg_img_star(im, cx, cy, r, r * 0.47f, 5, 0, col);
    pg_img_star(im, cx - 1, cy - 1.5f, r * 0.62f, r * 0.3f, 5, 0, alpha(C(0xFFFFFF), 110));
}

static void eyes(PG_Image *im, float x0, float x1, float y, float r, uint32_t white)
{
    pg_img_disc(im, x0, y, r, white);          pg_img_disc(im, x1, y, r, white);
    pg_img_disc(im, x0 + 0.4f, y + 0.6f, r * 0.58f, C(0x2B2A3C)); pg_img_disc(im, x1 + 0.4f, y + 0.6f, r * 0.58f, C(0x2B2A3C));
    pg_img_disc(im, x0 + 1.2f, y - 0.4f, r * 0.24f, C(0xFFFFFF)); pg_img_disc(im, x1 + 1.2f, y - 0.4f, r * 0.24f, C(0xFFFFFF));
}

PG_Image *paint_cube(int skin)
{
    static const uint32_t bg[N_CUBE_SKINS] = { 0xE8433A, 0x3E86E8, 0x4CB84A, 0x9A5BD8, 0x2BB5AE, 0xFF8FB8, 0x8DC63F, 0x59B7F2 };
    uint32_t base = C(bg[skin % N_CUBE_SKINS]);
    PG_Image *im = face_new(64, 64, base, 100u + (uint32_t)skin * 17u);
    switch (skin % N_CUBE_SKINS) {
    case 0: glyph_decal(im, &glyph_a, base, C(0xFFF4D6)); break;
    case 1: glyph_decal(im, &glyph_b, base, C(0xFFD23F)); break;
    case 2: glyph_decal(im, &glyph_1, base, C(0xFFFFFF)); break;
    case 3: star_decal(im, 32, 33, 22, base, C(0xFFD23F)); break;
    case 4: {   /* cat */
        tri(im, 13, 27, 15, 8, 30, 18, C(0xFF9F2E)); tri(im, 51, 27, 49, 8, 34, 18, C(0xFF9F2E));
        tri(im, 17, 24, 18, 13, 27, 19, C(0xFF7FA0)); tri(im, 47, 24, 46, 13, 37, 19, C(0xFF7FA0));
        pg_img_disc(im, 32, 36, 21, C(0xFFA83A));
        pg_img_disc(im, 32, 45, 10.5f, C(0xFFEBCF));
        for (int i = 0; i < 3; i++) pg_img_line(im, 32 + (float)(i - 1) * 5.0f, 17, 32 + (float)(i - 1) * 4.0f, 22, 2.2f, C(0xE67E1C));
        eyes(im, 23, 41, 34, 4.8f, C(0xFFFFFF));
        tri(im, 29.2f, 41, 34.8f, 41, 32, 44.4f, C(0xFF5F86));
        pg_img_line(im, 32, 44.4f, 32, 46.2f, 1.4f, C(0x8A4A2A));
        for (int s = -1; s <= 1; s += 2) { pg_img_line(im, 32, 46.2f, 32 + (float)s * 2.4f, 48, 1.4f, C(0x8A4A2A)); pg_img_line(im, 32 + (float)s * 2.4f, 48, 32 + (float)s * 4.6f, 46.8f, 1.4f, C(0x8A4A2A)); }
        pg_img_disc(im, 18.5f, 42, 3.6f, alpha(C(0xFF6F91), 140)); pg_img_disc(im, 45.5f, 42, 3.6f, alpha(C(0xFF6F91), 140));
        for (int s = -1; s <= 1; s += 2) {
            float x = 32 + (float)s * 9, xe = 32 + (float)s * 22;
            pg_img_line(im, x, 44, xe, 41.5f, 1.2f, C(0xFFFFFF)); pg_img_line(im, x, 46, xe, 47, 1.2f, C(0xFFFFFF));
        }
        break; }
    case 5: {   /* bunny */
        pg_img_line(im, 24, 28, 21, 9, 11, C(0xFFFFFF)); pg_img_line(im, 40, 28, 43, 9, 11, C(0xFFFFFF));
        pg_img_line(im, 24, 26, 22, 12, 5, C(0xFF9DBE));  pg_img_line(im, 40, 26, 42, 12, 5, C(0xFF9DBE));
        pg_img_disc(im, 32, 40, 20, C(0xFFFFFF));
        pg_img_disc(im, 32, 47, 9, C(0xFFF0F5));
        pg_img_disc(im, 24.5f, 38, 3.2f, C(0x3A2A4A)); pg_img_disc(im, 39.5f, 38, 3.2f, C(0x3A2A4A));
        pg_img_disc(im, 25.4f, 37, 1.1f, C(0xFFFFFF)); pg_img_disc(im, 40.4f, 37, 1.1f, C(0xFFFFFF));
        pg_img_disc(im, 32, 44, 2.6f, C(0xFF6F96));
        pg_img_line(im, 32, 45, 29, 49, 1.4f, C(0x9A5A6A)); pg_img_line(im, 32, 45, 35, 49, 1.4f, C(0x9A5A6A));
        pg_img_disc(im, 19, 44, 3.8f, alpha(C(0xFF8FB8), 150)); pg_img_disc(im, 45, 44, 3.8f, alpha(C(0xFF8FB8), 150));
        break; }
    case 6: {   /* bear */
        pg_img_disc(im, 17, 20, 8.5f, C(0xA2602F)); pg_img_disc(im, 47, 20, 8.5f, C(0xA2602F));
        pg_img_disc(im, 17, 20, 4.4f, C(0xE9B880)); pg_img_disc(im, 47, 20, 4.4f, C(0xE9B880));
        pg_img_disc(im, 32, 36, 21.5f, C(0xB06B38));
        pg_img_disc(im, 32, 44, 11.5f, C(0xEBC48E));
        pg_img_disc(im, 24, 31, 2.8f, C(0x2B2A3C)); pg_img_disc(im, 40, 31, 2.8f, C(0x2B2A3C));
        pg_img_disc(im, 24.9f, 30, 1, C(0xFFFFFF)); pg_img_disc(im, 40.9f, 30, 1, C(0xFFFFFF));
        pg_img_disc(im, 32, 40, 3.8f, C(0x3A2A2A));
        pg_img_line(im, 32, 43, 32, 46, 1.4f, C(0x6A4020)); pg_img_line(im, 32, 46, 28, 48.5f, 1.4f, C(0x6A4020)); pg_img_line(im, 32, 46, 36, 48.5f, 1.4f, C(0x6A4020));
        break; }
    default: {  /* duck */
        pg_img_disc(im, 32, 36, 21, C(0xFFD93B));
        pg_img_disc(im, 29, 14.5f, 3.4f, C(0xFFD93B)); pg_img_disc(im, 34.5f, 13.5f, 3, C(0xFFD93B));
        pg_img_disc(im, 17.5f, 43, 4.2f, alpha(C(0xFF9A6A), 150)); pg_img_disc(im, 46.5f, 43, 4.2f, alpha(C(0xFF9A6A), 150));
        rrect(im, 22.5f, 38.5f, 19, 11.5f, 5.5f, C(0xFF8A2A));
        pg_img_line(im, 26.5f, 43, 29, 43, 1.6f, C(0xC8581A)); pg_img_line(im, 35, 43, 37.5f, 43, 1.6f, C(0xC8581A));
        pg_img_line(im, 24.5f, 46.5f, 39.5f, 46.5f, 1.2f, C(0xE06E18));
        eyes(im, 23.5f, 40.5f, 31, 3.6f, C(0xFFFFFF));
        break; }
    }
    return face_done(im, base, 6);
}

PG_Image *paint_long(int skin)
{
    if (skin % N_LONG_SKINS == 0) {   /* a three-car toy train on a track */
        uint32_t base = C(0xFFD24A);
        PG_Image *im = face_new(128, 48, base, 300);
        static const uint32_t car[3] = { 0xE8433A, 0x3E86E8, 0x4CB84A };
        for (int i = 0; i < 3; i++) {
            float x = 9.0f + (float)i * 38.0f;
            rrect(im, x + 1, 13, 33, 24, 5, shade(C(car[i]), 0.55f));
            rrect(im, x, 11.5f, 33, 24, 5, C(car[i]));
            rrect(im, x + 3, 14, 27, 5, 2, alpha(C(0xFFFFFF), 70));
            rrect(im, x + 7, 21, 8.5f, 8, 2, C(0xFFF4D6)); rrect(im, x + 18, 21, 8.5f, 8, 2, C(0xFFF4D6));
            pg_img_disc(im, x + 8, 36.5f, 4.4f, C(0x4A4F63)); pg_img_disc(im, x + 25, 36.5f, 4.4f, C(0x4A4F63));
            pg_img_disc(im, x + 8, 36.5f, 1.7f, C(0xC8CCDC)); pg_img_disc(im, x + 25, 36.5f, 1.7f, C(0xC8CCDC));
            if (i < 2) pg_img_rect(im, (int)(x + 33), 28, 5, 2, C(0x4A4F63));
        }
        pg_img_rect(im, 6, 40, 116, 2, C(0x8A5A2C));
        return face_done(im, base, 6);
    } else {                          /* rainbow with two clouds */
        uint32_t base = C(0x8FD3FF);
        PG_Image *im = face_new(128, 48, base, 310);
        pg_img_gradient_v(im, 0, 0, 128, 48, C(0x7FC6FF), C(0xCDEBFF));
        static const uint32_t band[5] = { 0xE8433A, 0xFF9A2E, 0xFFD93B, 0x4CB84A, 0x3E86E8 };
        for (int i = 0; i < 5; i++) pg_img_ring(im, 64, 52, 41.0f - (float)i * 5.0f, 5.4f, C(band[i]));
        for (int s = 0; s < 2; s++) {
            float cx = s ? 112.0f : 16.0f;
            pg_img_disc(im, cx - 7, 38, 6.5f, C(0xFFFFFF)); pg_img_disc(im, cx + 2, 35, 8, C(0xFFFFFF)); pg_img_disc(im, cx + 10, 39, 6, C(0xFFFFFF));
            pg_img_disc(im, cx + 1, 41, 7, C(0xF4FAFF));
        }
        return face_done(im, base, 6);
    }
}

PG_Image *paint_tall(int skin)
{
    if (skin % N_TALL_SKINS == 0) {   /* rocket in a night sky */
        uint32_t base = C(0x33449E);
        PG_Image *im = face_new(48, 96, base, 400);
        pg_img_gradient_v(im, 0, 0, 48, 96, C(0x2A3993), C(0x5E78DA));
        for (int i = 0; i < 9; i++) pg_img_star(im, 8.0f + rndf() * 32.0f, 8.0f + rndf() * 80.0f, 2.6f, 1.1f, 4, rndf(), C(0xFFF3A8));
        tri(im, 14, 33, 34, 33, 24, 10, C(0xE8433A));
        rrect(im, 14, 31, 20, 40, 4, C(0xF4F6FF));
        rrect(im, 14, 31, 6, 40, 3, alpha(C(0x9AA6D8), 120));
        pg_img_disc(im, 24, 47, 6.6f, C(0xE8433A)); pg_img_disc(im, 24, 47, 4.8f, C(0x5CC4FF)); pg_img_disc(im, 22.8f, 45.8f, 1.5f, C(0xFFFFFF));
        rrect(im, 14, 61, 20, 4, 1.5f, C(0xE8433A));
        tri(im, 14, 56, 14, 74, 6.5f, 76, C(0xE8433A)); tri(im, 34, 56, 34, 74, 41.5f, 76, C(0xE8433A));
        tri(im, 18, 71, 30, 71, 24, 88, C(0xFF8A2A)); tri(im, 20.5f, 71, 27.5f, 71, 24, 81, C(0xFFE45C));
        return face_done(im, base, 6);
    } else if (skin % N_TALL_SKINS == 1) {   /* giraffe */
        uint32_t base = C(0xFFC83D);
        PG_Image *im = face_new(48, 96, base, 410);
        for (int i = 0; i < 12; i++) {       /* patches on a jittered grid */
            float w = 7.0f + rndf() * 3.0f, h = 6.0f + rndf() * 3.0f;
            rrect(im, 7.0f + (float)(i % 3) * 13.0f + rndf() * 3.0f, 52.0f + (float)(i / 3) * 10.0f + rndf() * 2.0f, w, h, 2.5f, alpha(C(0xB8702A), 235));
        }
        pg_img_line(im, 17, 13, 15.5f, 8, 2.6f, C(0xB8702A)); pg_img_line(im, 31, 13, 32.5f, 8, 2.6f, C(0xB8702A));
        pg_img_disc(im, 15.5f, 8, 2.4f, C(0x8A4A1A)); pg_img_disc(im, 32.5f, 8, 2.4f, C(0x8A4A1A));
        ellipse(im, 9, 24, 5, 3, C(0xE0A030)); ellipse(im, 39, 24, 5, 3, C(0xE0A030));
        rrect(im, 12, 15, 24, 34, 10, C(0xFFD25A));
        for (int i = 0; i < 4; i++) rrect(im, 15.0f + (float)(i % 2) * 14.0f, 17.0f + (float)(i / 2) * 5.0f, 5, 4, 1.5f, alpha(C(0xB8702A), 220));
        eyes(im, 18, 30, 29, 3.4f, C(0xFFFFFF));
        rrect(im, 15, 36.5f, 18, 12, 6, C(0xFFEBC4));
        pg_img_disc(im, 20.5f, 41.5f, 1.4f, C(0x8A4A2A)); pg_img_disc(im, 27.5f, 41.5f, 1.4f, C(0x8A4A2A));
        pg_img_line(im, 20, 45.5f, 28, 45.5f, 1.2f, C(0xC8785A));
        return face_done(im, base, 6);
    } else {                          /* friendly robot */
        uint32_t base = C(0xFF8A2A);
        PG_Image *im = face_new(48, 96, base, 420);
        pg_img_line(im, 24, 14, 24, 8.5f, 2.2f, C(0x8C96B0)); pg_img_disc(im, 24, 8.5f, 3.2f, C(0xE8433A)); pg_img_disc(im, 23.2f, 7.6f, 1.1f, C(0xFFFFFF));
        rrect(im, 9, 16, 30, 26, 6, shade(C(0x8C96B0), 0.7f));
        rrect(im, 9, 14.5f, 30, 26, 6, C(0xD9E2F2));
        rrect(im, 11, 16, 12, 4, 2, alpha(C(0xFFFFFF), 180));
        rrect(im, 13, 21, 8, 8, 2, C(0x3E86E8)); rrect(im, 27, 21, 8, 8, 2, C(0x3E86E8));
        pg_img_disc(im, 17, 25, 2.2f, C(0xFFF4A0)); pg_img_disc(im, 31, 25, 2.2f, C(0xFFF4A0));
        for (int i = 0; i < 4; i++) pg_img_line(im, 17.0f + (float)i * 4.6f, 33, 17.0f + (float)i * 4.6f, 37, 1.6f, C(0x6A7494));
        rrect(im, 5, 46, 6, 22, 3, C(0x8C96B0)); rrect(im, 37, 46, 6, 22, 3, C(0x8C96B0));
        rrect(im, 12, 45.5f, 24, 28, 5, shade(C(0x8C96B0), 0.7f));
        rrect(im, 12, 44, 24, 28, 5, C(0xD9E2F2));
        rrect(im, 16, 49, 16, 13, 3, C(0x4A5578));
        pg_img_disc(im, 20, 55.5f, 2.4f, C(0xE8433A)); pg_img_disc(im, 26, 55.5f, 2.4f, C(0x4CB84A)); pg_img_disc(im, 31, 55.5f, 2, C(0xFFD23F));
        pg_img_star(im, 24, 67, 3.6f, 1.7f, 5, 0, C(0xFFD23F));
        rrect(im, 14, 76, 8, 10, 2, C(0x8C96B0)); rrect(im, 26, 76, 8, 10, 2, C(0x8C96B0));
        return face_done(im, base, 6);
    }
}

PG_Image *paint_flat(int skin)
{
    (void)skin;   /* candy stripes */
    uint32_t base = C(0xE8433A);
    PG_Image *im = face_new(96, 32, base, 500);
    pg_img_stripes(im, 8, 8, C(0xE8433A), C(0xFFF1DA));
    grain(im, C(0x7A2A20), 40, 30, 0);
    return face_done(im, base, 6);
}

PG_Image *paint_ball(int which)
{
    PG_Image *im = pg_img_new(64, 64, 0xFFFFFFFFu);
    seed(600u + (uint32_t)which);
    if (which == 0) {   /* beach ball: six panels */
        static const uint32_t col[6] = { 0xF0463C, 0xFFFFFF, 0x2D7DF0, 0xFFFFFF, 0xFFC928, 0xFFFFFF };
        for (int i = 0; i < 6; i++) {
            int x0 = i * 64 / 6, x1 = (i + 1) * 64 / 6;
            pg_img_rect(im, x0, 0, x1 - x0, 64, C(col[i]));
            pg_img_rect(im, x0, 0, 1, 64, shade(C(col[i]), 0.82f));
        }
        pg_img_rect(im, 0, 0, 64, 6, alpha(C(0xFFFFFF), 150));
        pg_img_rect(im, 0, 58, 64, 6, alpha(C(0xFFFFFF), 150));
    } else {            /* star ball: purple with a white band of golden stars (drawn narrow: the sphere stretches u 2:1) */
        pg_img_gradient_v(im, 0, 0, 64, 64, C(0x8A55E8), C(0x6A3BC8));
        pg_img_rect(im, 0, 20, 64, 24, C(0xFFF4D6));
        pg_img_rect(im, 0, 20, 64, 2, shade(C(0xFFF4D6), 0.80f)); pg_img_rect(im, 0, 42, 64, 2, shade(C(0xFFF4D6), 0.80f));
        for (int i = 0; i < 4; i++) star_xy(im, 8.0f + (float)i * 16.0f, 32.5f, 6.4f, 11.5f, C(0xFF9A2E));
        for (int i = 0; i < 4; i++) { star_xy(im, 16.0f + (float)i * 16.0f, 9.5f, 2.6f, 4.8f, C(0xFFD23F)); star_xy(im, 16.0f + (float)i * 16.0f, 55.5f, 2.6f, 4.8f, C(0xFFD23F)); }
    }
    pg_img_noise(im, 4, 17);
    pg_img_ps1(im);
    return im;
}

/* -------------------------------------------------------------------- room */
PG_Image *paint_floor(void)
{
    /* two planks, each 32 px deep; butt joints staggered; seamless left-right */
    PG_Image *im = pg_img_new(128, 64, C(0xD9A15F));
    seed(700);
    static const uint32_t pc[2] = { 0xDDA462, 0xCF9355 };
    for (int p = 0; p < 2; p++) {
        int y0 = p * 32;
        pg_img_rect(im, 0, y0, 128, 32, C(pc[p]));
        pg_img_gradient_v(im, 0, y0, 128, 32, shade(C(pc[p]), 1.06f), shade(C(pc[p]), 0.94f));
    }
    /* each plank gets its own tone shift on both sides of the joint */
    pg_img_rect(im, 90, 0, 38, 32, alpha(C(0xFFE0A0), 22));
    pg_img_rect(im, 0, 32, 42, 32, alpha(C(0x7A4A20), 26));
    grain(im, C(0x8A5528), 260, 60, 1);
    grain(im, C(0xF2C888), 120, 46, 1);
    /* knots */
    ellipse(im, 28, 15, 5, 3, C(0xA8703A)); ellipse(im, 28, 15, 3, 1.8f, C(0x8A5528));
    ellipse(im, 96, 47, 4.5f, 2.6f, C(0xA06A34)); ellipse(im, 96, 47, 2.6f, 1.5f, C(0x80502A));
    /* seams */
    for (int p = 0; p < 2; p++) {
        pg_img_rect(im, 0, p * 32, 128, 2, C(0x7A4A24));
        pg_img_rect(im, 0, p * 32 + 2, 128, 1, alpha(C(0xFFE6B0), 150));
    }
    pg_img_rect(im, 88, 0, 2, 32, C(0x7A4A24)); pg_img_rect(im, 90, 0, 1, 32, alpha(C(0xFFE6B0), 120));
    pg_img_rect(im, 40, 32, 2, 32, C(0x7A4A24)); pg_img_rect(im, 42, 32, 1, 32, alpha(C(0xFFE6B0), 120));
    /* nail heads */
    for (int i = 0; i < 2; i++) { pg_img_disc(im, 94, 6.0f + (float)i * 20.0f, 1.0f, C(0x6A4020)); pg_img_disc(im, 46, 38.0f + (float)i * 20.0f, 1.0f, C(0x6A4020)); }
    pg_img_noise(im, 5, 5);
    pg_img_ps1(im);
    return im;
}

PG_Image *paint_wallpaper(void)
{
    PG_Image *im = pg_img_new(128, 128, C(0xFFEBC8));
    seed(710);
    for (int i = 0; i < 4; i++) pg_img_rect(im, i * 32 + 8, 0, 16, 128, C(0xFFE1B4));
    for (int i = 0; i < 4; i++) { pg_img_rect(im, i * 32 + 8, 0, 1, 128, alpha(C(0xFFFFFF), 110)); pg_img_rect(im, i * 32 + 23, 0, 1, 128, alpha(C(0xE9C690), 120)); }
    pg_img_star(im, 32, 34, 8.5f, 4, 5, 0.1f, C(0xFFCB5A));
    pg_img_star(im, 96, 98, 8.5f, 4, 5, -0.1f, C(0xFFCB5A));
    pg_img_disc(im, 96, 34, 3.4f, C(0xFFA9BC)); pg_img_disc(im, 32, 98, 3.4f, C(0xFFA9BC));
    pg_img_disc(im, 64, 66, 2.6f, C(0xA9D4FF)); pg_img_disc(im, 4, 2, 2.6f, C(0xA9D4FF)); pg_img_disc(im, 124, 126, 2.6f, C(0xA9D4FF));
    pg_img_noise(im, 4, 9);
    pg_img_ps1(im);
    return im;
}

PG_Image *paint_wainscot(void)
{
    PG_Image *im = pg_img_new(64, 64, C(0xF7E9D0));
    seed(720);
    pg_img_gradient_v(im, 0, 0, 64, 64, C(0xFBEFD9), C(0xEDDBBC));
    grain(im, C(0xC8A874), 90, 40, 1);
    for (int i = 0; i < 4; i++) {
        pg_img_rect(im, i * 16, 0, 2, 64, C(0xC9AE82)); pg_img_rect(im, i * 16 + 2, 0, 1, 64, alpha(C(0xFFFFFF), 160));
    }
    pg_img_noise(im, 4, 13);
    pg_img_ps1(im);
    return im;
}

PG_Image *paint_rug(void)
{
    PG_Image *im = pg_img_new(128, 128, 0x00FFFFFFu);
    seed(740);
    pg_img_disc(im, 64, 64, 63, C(0xFFF1D6));
    pg_img_disc(im, 64, 64, 57, C(0x2FB5A5));
    for (int i = 0; i < 24; i++) { float a = PG_TAU * (float)i / 24.0f; pg_img_disc(im, 64 + cosf(a) * 52.5f, 64 + sinf(a) * 52.5f, 2.3f, C(0xFFFFFF)); }
    pg_img_disc(im, 64, 64, 47, C(0xFFD057));
    pg_img_disc(im, 64, 64, 42, C(0xF2787F));
    pg_img_ring(im, 64, 64, 35, 2.4f, C(0xFFF1D6));
    pg_img_disc(im, 64, 64, 29, C(0x4FA3E8));
    pg_img_star(im, 64, 65, 22, 10, 5, 0, C(0xFFD23F));
    pg_img_star(im, 63, 64, 13, 6, 5, 0, alpha(C(0xFFFFFF), 150));
    for (int i = 0; i < 400; i++) {   /* woolly fuzz */
        float a = rndf() * PG_TAU, r = rndf() * 60.0f;
        put(im, 64 + (int)(cosf(a) * r), 64 + (int)(sinf(a) * r), alpha(C(0xFFFFFF), 40), 1.0f, 0);
    }
    pg_img_noise(im, 5, 23);
    pg_img_ps1(im);
    return im;
}

PG_Image *paint_view(void)
{
    PG_Image *im = pg_img_new(96, 96, C(0x7FC8FF));
    seed(750);
    pg_img_gradient_v(im, 0, 0, 96, 96, C(0x6FBFFF), C(0xDDF3FF));
    pg_img_disc(im, 68, 24, 20, alpha(C(0xFFF6B0), 60)); pg_img_disc(im, 68, 24, 14, alpha(C(0xFFF2A0), 110));
    pg_img_disc(im, 68, 24, 9.5f, C(0xFFE45C));
    for (int i = 0; i < 2; i++) {
        float cx = i ? 24.0f : 52.0f, cy = i ? 40.0f : 14.0f;
        pg_img_disc(im, cx - 7, cy + 2, 6, C(0xFFFFFF)); pg_img_disc(im, cx + 1, cy - 2, 8, C(0xFFFFFF)); pg_img_disc(im, cx + 9, cy + 2, 6.5f, C(0xFFFFFF));
        pg_img_rect(im, (int)(cx - 12), (int)(cy + 2), 28, 5, C(0xFFFFFF));
        pg_img_rect(im, (int)(cx - 12), (int)(cy + 6), 28, 2, alpha(C(0xCFE6FA), 200));
    }
    for (int x = 0; x < 96; x++) {   /* two layers of rolling hills */
        float h1 = 62.0f + 7.0f * sinf((float)x * 0.09f + 1.0f), h2 = 72.0f + 6.0f * sinf((float)x * 0.13f + 4.0f);
        pg_img_rect(im, x, (int)h1, 1, 96 - (int)h1, C(0x93D683));
        pg_img_rect(im, x, (int)h2, 1, 96 - (int)h2, C(0x5DB86A));
    }
    pg_img_rect(im, 0, 88, 96, 8, C(0x4AA85A));
    /* little tree and flowers on the near hill */
    pg_img_rect(im, 20, 66, 3, 10, C(0x8A5A2C)); pg_img_disc(im, 21.5f, 62, 8, C(0x3E9A4C)); pg_img_disc(im, 18, 65, 5, C(0x4AAA58));
    for (int i = 0; i < 7; i++) { float x = 30.0f + (float)i * 9.0f; pg_img_disc(im, x, 84.0f + (float)(i & 1) * 4.0f, 1.8f, i & 1 ? C(0xFF8FB8) : C(0xFFE45C)); }
    pg_img_noise(im, 4, 25);
    pg_img_ps1(im);
    return im;
}

PG_Image *paint_chest_front(void)
{
    PG_Image *im = pg_img_new(96, 80, C(0x3D7FDB));
    seed(760);
    pg_img_gradient_v(im, 0, 0, 96, 80, C(0x4A8CE8), C(0x336FC4));
    grain(im, C(0x1F4F9A), 150, 46, 0);
    grain(im, C(0x7CB4FF), 70, 36, 0);
    for (int i = 0; i < 6; i++) { pg_img_rect(im, i * 16, 0, 2, 80, C(0x24509C)); pg_img_rect(im, i * 16 + 2, 0, 1, 80, alpha(C(0xA8D0FF), 150)); }
    for (int s = 0; s < 2; s++) {   /* golden straps with rivets */
        int x = s ? 76 : 10;
        pg_img_rect(im, x - 1, 0, 12, 80, C(0xB8841E)); pg_img_rect(im, x, 0, 10, 80, C(0xF5C03A)); pg_img_rect(im, x + 1, 0, 2, 80, alpha(C(0xFFF0A0), 170));
        for (int k = 0; k < 4; k++) { pg_img_disc(im, (float)x + 5, 10.0f + (float)k * 20.0f, 2, C(0xB8841E)); pg_img_disc(im, (float)x + 4.6f, 9.6f + (float)k * 20.0f, 1.2f, C(0xFFE890)); }
    }
    pg_img_rect(im, 0, 0, 96, 3, C(0xFFD65A)); pg_img_rect(im, 0, 3, 96, 2, C(0xB8841E));
    pg_img_frame(im, 0, 0, 96, 80, 2, C(0x24509C));
    pg_img_noise(im, 4, 27);
    pg_img_ps1(im);
    return im;
}

PG_Image *paint_button(void)
{
    PG_Image *im = pg_img_new(64, 64, 0x00FFFFFFu);
    seed(770);
    pg_img_disc(im, 32, 32, 31.5f, C(0xB62A22));
    pg_img_gradient_radial(im, 30, 28, 30, C(0xFF7B52), C(0xE03A2E));
    pg_img_ring(im, 32, 32, 28.5f, 2.2f, alpha(C(0xFFFFFF), 200));
    pg_img_ring(im, 32, 32, 31, 2, C(0xB62A22));
    pg_img_disc(im, 32, 32, 25, alpha(C(0xFFFFFF), 35));
    /* a little block pyramid with a star on top */
    static const struct { float x, y; uint32_t c; } b[3] = { { 15.5f, 34, 0xFFD23F }, { 32.5f, 34, 0x3E86E8 }, { 24, 18.5f, 0x4CB84A } };
    for (int i = 0; i < 3; i++) {
        rrect(im, b[i].x + 1.4f, b[i].y + 1.8f, 16.5f, 16.5f, 2.4f, alpha(C(0x7A1A14), 190));
        rrect(im, b[i].x, b[i].y, 16.5f, 16.5f, 2.4f, shade(C(b[i].c), 0.78f));
        rrect(im, b[i].x + 1.4f, b[i].y + 1.4f, 13.7f, 13.7f, 1.8f, C(b[i].c));
        rrect(im, b[i].x + 2.6f, b[i].y + 2.4f, 8, 2.6f, 1.2f, alpha(C(0xFFFFFF), 110));
    }
    pg_img_star(im, 32.5f, 12, 7.4f, 3.4f, 5, 0, C(0xFFF4A0));
    pg_img_star(im, 32.5f, 12.4f, 4.4f, 2, 5, 0, C(0xFFFFFF));
    pg_img_noise(im, 3, 29);
    pg_img_ps1(im);
    return im;
}

PG_Image *paint_glove(void)
{
    PG_Image *im = pg_img_new(64, 64, 0x00FFFFFFu);
    uint32_t ol = C(0x7D8DB0), wh = C(0xFFFFFF), sh = C(0xDCE3F2);
    for (int pass = 0; pass < 2; pass++) {   /* pass 0: outline (grown), pass 1: fill */
        float g = pass == 0 ? 1.9f : 0.0f;
        uint32_t c = pass == 0 ? ol : wh;
        pg_img_line(im, 27, 40, 27, 11, 10.5f + 2 * g, c);                 /* index finger */
        pg_img_line(im, 37, 40, 37, 31, 9.5f + 2 * g, c);                  /* curled fingers */
        pg_img_line(im, 45, 42, 45, 35, 8.5f + 2 * g, c);
        pg_img_line(im, 19, 47, 11.5f, 37.5f, 8.5f + 2 * g, c);            /* thumb */
        rrect(im, 16.0f - g, 36.0f - g, 33.0f + 2 * g, 22.0f + 2 * g, 9, c);   /* palm */
    }
    pg_img_line(im, 32, 36, 32, 46, 1.4f, sh); pg_img_line(im, 41, 38, 41, 48, 1.4f, sh);
    pg_img_line(im, 25, 7.5f, 25, 20, 2.2f, alpha(C(0xFFFFFF), 255));
    rrect(im, 19.5f, 55, 26.5f, 9, 3, C(0x7D8DB0));
    rrect(im, 20.5f, 55.5f, 24.5f, 8, 2.6f, C(0xE8433A));
    pg_img_rect(im, 22, 58, 21, 2, alpha(C(0xFFFFFF), 150));
    pg_img_ps1(im);
    return im;
}

PG_Image *paint_fabric(void)
{
    PG_Image *im = pg_img_new(16, 16, C(0xF0F0F0));
    pg_img_noise(im, 12, 33);
    pg_img_ps1(im);
    return im;
}

PG_Image *paint_poster(void)
{
    PG_Image *im = pg_img_new(64, 80, C(0xA8DCFF));
    seed(780);
    pg_img_gradient_v(im, 0, 0, 64, 80, C(0x78C4FF), C(0xFFF0C8));
    for (int i = 0; i < 2; i++) {
        float cx = i ? 46.0f : 18.0f, cy = i ? 22.0f : 52.0f;
        pg_img_disc(im, cx - 5, cy + 1, 4.5f, C(0xFFFFFF)); pg_img_disc(im, cx + 1, cy - 2, 6, C(0xFFFFFF)); pg_img_disc(im, cx + 7, cy + 1, 4.5f, C(0xFFFFFF));
        pg_img_rect(im, (int)(cx - 9), (int)(cy + 1), 21, 4, C(0xFFFFFF));
    }
    for (int x = 0; x < 64; x++) {
        float h1 = 62.0f + 5.0f * sinf((float)x * 0.11f + 0.5f), h2 = 69.0f + 4.0f * sinf((float)x * 0.17f + 3.0f);
        pg_img_rect(im, x, (int)h1, 1, 80 - (int)h1, C(0x93D683));
        pg_img_rect(im, x, (int)h2, 1, 80 - (int)h2, C(0x5DB86A));
    }
    /* the balloon: coloured gores, a basket and two ropes */
    static const uint32_t gore[5] = { 0xE8433A, 0xFFD23F, 0x3E86E8, 0xFFD23F, 0xE8433A };
    pg_img_disc(im, 32, 28, 16.5f, C(0xB62A22));
    for (int j = 12; j < 46; j++)
        for (int i = 14; i < 51; i++) {
            float dx = (float)i + 0.5f - 32.0f, dy = (float)j + 0.5f - 28.0f;
            if (dx * dx + dy * dy < 15.5f * 15.5f) put(im, i, j, C(gore[(int)((dx + 15.5f) / 6.3f) % 5]), 1.0f, 0);
        }
    pg_img_disc(im, 28, 22, 5, alpha(C(0xFFFFFF), 70));
    tri(im, 22, 40, 42, 40, 32, 46, C(0xB62A22));
    pg_img_line(im, 27, 44, 29, 52, 1.0f, C(0x7A5A3A)); pg_img_line(im, 37, 44, 35, 52, 1.0f, C(0x7A5A3A));
    rrect(im, 27, 51, 10, 7, 1.5f, C(0xC8883E)); pg_img_rect(im, 27, 53, 10, 1, C(0x8A5A2C));
    pg_img_frame(im, 0, 0, 64, 80, 2, alpha(C(0xFFFFFF), 90));
    pg_img_noise(im, 4, 31);
    pg_img_ps1(im);
    return im;
}
