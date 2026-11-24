/* scene.c - Draw & Roll: the toy theatre.
 *
 *   back  (BG list, drawn first): a painted sky/meadow mural on a big panel, a smiling sun, drifting clouds, two quilted
 *         cushion side walls that receive the ramps' soft shadows
 *   front (depth sorted): two grassy hills that funnel every ball into the bucket, little toy trees and flowers
 * The hills and walls are mirrored by static physics segments with the same outlines.
 */
#include "scene.h"

#include <string.h>

#define NH      6                  /* points per hill outline */
#define HILL_T  14.0f              /* half thickness of the hill physics segments (surface = top of the segment) */
#define PANEL_W 1440.0f
#define PANEL_H 780.0f
#define VW      384                /* mural canvas in texels */
#define VH      208
#define TILE_W  128
#define TILE_H  104

static const float HX[NH] = { -458, -400, -334, -262, -190, -MOUTH_X };
static const float HY[NH] = {  -52,  -79, -106, -130, -149, RIM_Y };

static PG_Mesh *m_tile, *m_wall[2], *m_hilltop[2], *m_hillfront[2];
static PG_Mesh *m_trunk, *m_cone, *m_lolli;
static PG_Tex   t_mural[6], t_quilt, t_hilltop, t_hillfront, t_cloudspr, t_sunrays, t_sunface;
static PG_Tex   t_tree_cone, t_tree_trunk, t_tree_lolli, t_flower[3];
static int      wall_id[3] = { 0, 1, 2 };            /* 2 = the ceiling: no cushion to bulge */
static float    wall_bulge[2], t_now;

/* ------------------------------------------------------------------ helpers */
static uint32_t rs = 0x2468ACEu;
static float rnd(void)
{
    rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
    return (float)(rs >> 8) * (1.0f / 16777216.0f);
}

/* x of hill outline point i for the given side (0 left, 1 right) */
static float hx(int side, int i) { return side ? -HX[NH - 1 - i] : HX[i]; }
static float hy(int side, int i) { return side ? HY[NH - 1 - i] : HY[i]; }

static float hill_surface(int side, float x)
{
    for (int i = 0; i + 1 < NH; i++) {
        float x0 = hx(side, i), x1 = hx(side, i + 1);
        if (x >= x0 && x <= x1) return lerpf(hy(side, i), hy(side, i + 1), (x - x0) / (x1 - x0));
    }
    return x < 0 ? HY[0] : RIM_Y;
}

/* ------------------------------------------------------------------ painting */
static void paint_ridge(PG_Image *im, float base, float a1, float f1, float p1, float a2, float f2, float p2, int bottom, uint32_t c_top, uint32_t c_bot)
{
    for (int x = 0; x < im->w; x++) {
        float yt = base - a1 * sinf((float)x * f1 + p1) - a2 * sinf((float)x * f2 + p2);
        int y0 = (int)yt;
        for (int y = y0 < 0 ? 0 : y0; y < bottom && y < im->h; y++)
            im->px[y * im->w + x] = pg_argb_lerp(c_top, c_bot, (float)(y - y0) / (float)(bottom - y0 + 1));
    }
}

static float ridge_y(float base, float a1, float f1, float p1, float a2, float f2, float p2, float x)
{
    return base - a1 * sinf(x * f1 + p1) - a2 * sinf(x * f2 + p2);
}

static void paint_mural(void)
{
    PG_Image *im = pg_img_new(VW, VH, PG_RGB24(0x8ACBFF));
    pg_img_gradient_v(im, 0, 0, VW, 96, PG_RGB24(0x58B0FF), PG_RGB24(0x9AD8FF));
    pg_img_gradient_v(im, 0, 96, VW, 40, PG_RGB24(0x9AD8FF), PG_RGB24(0xFFEBC8));
    /* a few faraway painted clouds (the real, drifting ones are sprites in front of the panel) */
    static const float cl[6][3] = { { 60, 46, 1.0f }, { 188, 24, 0.8f }, { 300, 58, 1.1f }, { 128, 74, 0.7f }, { 350, 22, 0.6f }, { 238, 84, 0.65f } };
    for (int k = 0; k < 6; k++) {
        float cx = cl[k][0], cy = cl[k][1], s = cl[k][2];
        pg_img_disc(im, cx, cy, 11 * s, PG_ARGB(255, 255, 255, 255));
        pg_img_disc(im, cx + 13 * s, cy - 5 * s, 14 * s, PG_ARGB(255, 255, 255, 255));
        pg_img_disc(im, cx + 29 * s, cy - 1 * s, 12 * s, PG_ARGB(255, 255, 255, 255));
        pg_img_disc(im, cx + 14 * s, cy + 2 * s, 11 * s, PG_ARGB(255, 255, 255, 255));
        pg_img_rect(im, (int)(cx - 4 * s), (int)(cy + 5 * s), (int)(38 * s), (int)(5 * s), PG_ARGB(255, 236, 244, 255));
    }
    paint_ridge(im, 112, 17, 0.021f, 0.8f, 8, 0.057f, 2.2f, 150, PG_RGB24(0xB0CDF0), PG_RGB24(0xCFE2F6));    /* hazy mountains */
    paint_ridge(im, 129, 12, 0.026f, 2.0f, 6, 0.070f, 0.5f, 160, PG_RGB24(0x96D7B4), PG_RGB24(0xB4E6BE));    /* rolling hills */
    paint_ridge(im, 148, 8, 0.035f, 1.0f, 3, 0.090f, 0.0f, VH, PG_RGB24(0x8CE078), PG_RGB24(0x62C068));      /* meadow */
    for (int k = 0; k < 16; k++) {                                       /* tiny round trees on the hills */
        float x = 8.0f + rnd() * (VW - 16), y = ridge_y(129, 12, 0.026f, 2.0f, 6, 0.070f, 0.5f, x) + 3.0f;
        pg_img_rect(im, (int)x, (int)y - 2, 1, 4, PG_RGB24(0x8A6240));
        pg_img_disc(im, x + 0.5f, y - 4.0f, 3.4f, PG_RGB24(0x4FB070));
        pg_img_disc(im, x - 0.3f, y - 5.0f, 1.6f, PG_RGB24(0x78D090));
    }
    for (int k = 0; k < 150; k++) {                                      /* flowers in the meadow */
        static const uint32_t fc[4] = { 0xFFFFFF, 0xFFE25A, 0xFF9CC8, 0xFFB45A };
        float x = rnd() * VW, y = 154.0f + rnd() * 52.0f;
        pg_img_disc(im, x, y, 0.9f + rnd() * 0.8f, PG_RGB24(fc[k & 3]));
    }
    pg_img_noise(im, 3, 77);
    for (int r = 0; r < 2; r++)
        for (int c = 0; c < 3; c++) {
            PG_Image *tile = pg_img_new(TILE_W, TILE_H, 0xFFFFFFFFu);
            for (int j = 0; j < TILE_H; j++) memcpy(&tile->px[j * TILE_W], &im->px[(r * TILE_H + j) * VW + c * TILE_W], TILE_W * 4);
            t_mural[r * 3 + c] = tex_finish(tile, true);
        }
    pg_img_free(im);
}

static void paint_quilt(void)
{
    /* puffy diamond quilting: seams on x+y = 32 (mod 64) and x-y = 32 (mod 64), a button on every seam crossing */
    PG_Image *im = pg_img_new(64, 64, PG_RGB24(0xFFEFF3));
    for (int j = 0; j < 64; j++)
        for (int i = 0; i < 64; i++) {
            int u = ((i + j + 32) & 63) - 32, v = ((i - j + 32) & 63) - 32;         /* signed distances to the seam lines */
            float d = (float)(abs(u) > abs(v) ? abs(u) : abs(v)) / 32.0f;            /* 0 at a diamond centre, 1 at its seam */
            float puff = 1.0f - d * sqrtf(d);                                        /* a lit hump in the middle of every diamond */
            int r = 220 + (int)(35.0f * puff), g = 178 + (int)(68.0f * puff), b = 204 + (int)(46.0f * puff);
            im->px[j * 64 + i] = PG_RGB24(((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
        }
    for (int k = -2; k <= 4; k++) {
        pg_img_line(im, (float)(32 * k), 0, (float)(32 * k + 64), 64, 3.0f, PG_ARGB(120, 176, 96, 138));
        pg_img_line(im, (float)(32 * k), 64, (float)(32 * k + 64), 0, 3.0f, PG_ARGB(120, 176, 96, 138));
    }
    static const int bx[6] = { 32, 0, 64, 32, 0, 64 }, by[6] = { 0, 32, 32, 64, 0, 64 };
    for (int k = 0; k < 6; k++) {
        pg_img_disc(im, (float)bx[k] + 0.7f, (float)by[k] + 0.9f, 5.2f, PG_ARGB(120, 120, 50, 90));
        pg_img_disc(im, (float)bx[k], (float)by[k], 4.8f, PG_RGB24(0xE2689A));
        pg_img_disc(im, (float)bx[k] - 1.3f, (float)by[k] - 1.4f, 2.0f, PG_RGB24(0xFFC8DE));
    }
    pg_img_noise(im, 3, 12);
    t_quilt = tex_finish(im, true);
}

static void paint_hill(void)
{
    PG_Image *sheet = pg_img_new(96, 64, 0x00FFFFFFu);                      /* grass top (64x64) and hill front (32x64) */
    PG_Image *im = pg_img_new(64, 64, PG_RGB24(0xA0DE50));
    for (int j = 0; j < 64; j += 16) pg_img_rect(im, 0, j, 64, 8, PG_ARGB(70, 214, 250, 110));
    for (int k = 0; k < 9; k++) {
        static const uint32_t fc[3] = { 0xFFFFFF, 0xFFE25A, 0xFF9CC8 };
        pg_img_disc(im, rnd() * 64.0f, rnd() * 64.0f, 1.4f, PG_RGB24(fc[k % 3]));
    }
    pg_img_noise(im, 6, 5);
    img_blit(sheet, im, 0, 0);
    pg_img_free(im);

    im = pg_img_new(32, 64, PG_RGB24(0x5CC262));
    pg_img_gradient_v(im, 0, 0, 32, 64, PG_RGB24(0x6CD268), PG_RGB24(0x2E9C78));
    for (int k = 0; k < 26; k++) {                                          /* grass blades: short vertical strokes */
        float x = rnd() * 32.0f, y = 8.0f + rnd() * 52.0f;
        pg_img_line(im, x, y, x + (rnd() - 0.5f) * 2.0f, y - 3.0f - rnd() * 5.0f, 1.3f, PG_ARGB(130, 150, 230, 110));
    }
    for (int k = 0; k < 6; k++) pg_img_disc(im, rnd() * 32.0f, 12.0f + rnd() * 48.0f, 1.6f + rnd() * 2.6f, PG_ARGB(80, 28, 120, 90));
    pg_img_rect(im, 0, 0, 32, 5, PG_RGB24(0xA4E45A));                       /* the bright grassy lip */
    pg_img_rect(im, 0, 5, 32, 2, PG_RGB24(0x5CBC60));
    for (int k = 0; k < 4; k++) {                                           /* daisies */
        float cx = 4.0f + rnd() * 24.0f, cy = 14.0f + rnd() * 38.0f;
        for (int p = 0; p < 5; p++) {
            float a = (float)p * PG_TAU / 5.0f;
            pg_img_disc(im, cx + cosf(a) * 2.1f, cy + sinf(a) * 2.1f, 1.5f, PG_RGB24(k & 1 ? 0xFFFFFF : 0xFFC4DC));
        }
        pg_img_disc(im, cx, cy, 1.3f, PG_RGB24(0xFFD84A));
    }
    pg_img_noise(im, 4, 6);
    img_blit(sheet, im, 64, 0);
    pg_img_free(im);
    PG_Tex all = tex_finish(sheet, true);
    t_hilltop = tex_sub(all, 96, 64, 0, 0, 64, 64);
    t_hillfront = tex_sub(all, 96, 64, 64, 0, 32, 64);
}

static void paint_sky_sprites(void)
{
    PG_Image *sheet = pg_img_new(128, 128, 0x00FFFFFFu);                     /* drifting cloud (128x64) over smiling sun (64x64) */
    PG_Image *im = pg_img_new(128, 64, 0x00FFFFFFu);                         /* drifting cloud */
    pg_img_disc(im, 34, 40, 15, 0xFFFFFFFFu); pg_img_disc(im, 56, 32, 20, 0xFFFFFFFFu);
    pg_img_disc(im, 84, 34, 19, 0xFFFFFFFFu); pg_img_disc(im, 104, 42, 14, 0xFFFFFFFFu);
    pg_img_disc(im, 70, 44, 18, 0xFFFFFFFFu);
    pg_img_rect(im, 22, 44, 92, 9, 0xFFFFFFFFu);
    pg_img_rect(im, 24, 50, 88, 4, PG_ARGB(110, 196, 220, 255));
    img_blit(sheet, im, 0, 0);
    pg_img_free(im);

    im = pg_img_new(128, 128, 0x00FFFFFFu);                                  /* sun rays */
    pg_img_star(im, 64, 64, 62, 40, 12, 0.0f, PG_ARGB(255, 255, 214, 84));
    t_sunrays = tex_finish(im, true);

    im = pg_img_new(64, 64, 0x00FFFFFFu);                                    /* smiling sun */
    pg_img_disc(im, 32, 32, 30, PG_RGB24(0xFFD84A));
    pg_img_disc(im, 29, 29, 25, PG_RGB24(0xFFE866));
    pg_img_disc(im, 24, 28, 3.0f, PG_RGB24(0x6A3A20)); pg_img_disc(im, 40, 28, 3.0f, PG_RGB24(0x6A3A20));
    pg_img_disc(im, 17, 37, 5.0f, PG_ARGB(150, 255, 130, 120)); pg_img_disc(im, 47, 37, 5.0f, PG_ARGB(150, 255, 130, 120));
    for (int k = 0; k < 8; k++) {
        float t0 = (float)k / 8.0f, t1 = (float)(k + 1) / 8.0f;
        pg_img_line(im, 22.0f + 20.0f * t0, 39.0f + 8.0f * 4.0f * t0 * (1.0f - t0), 22.0f + 20.0f * t1, 39.0f + 8.0f * 4.0f * t1 * (1.0f - t1), 2.6f, PG_RGB24(0x8A3A20));
    }
    img_blit(sheet, im, 0, 64);
    pg_img_free(im);
    PG_Tex all = tex_finish(sheet, true);
    t_cloudspr = tex_sub(all, 128, 128, 0, 0, 128, 64);
    t_sunface = tex_sub(all, 128, 128, 0, 64, 64, 64);
}

static void paint_trees(void)
{
    PG_Image *sheet = pg_img_new(96, 64, 0x00FFFFFFu);                       /* cone | apple | trunk on the top row, flowers below */
    PG_Image *im = pg_img_new(32, 32, PG_RGB24(0x3FB06A));
    pg_img_stripes(im, 5, 6, PG_RGB24(0x3FB06A), PG_RGB24(0x5CCB78));
    pg_img_noise(im, 5, 3);
    img_blit(sheet, im, 0, 0);
    pg_img_free(im);

    im = pg_img_new(16, 16, PG_RGB24(0xA8744A));
    pg_img_stripes(im, 3, 0, PG_RGB24(0xA8744A), PG_RGB24(0x8C5A38));
    img_blit(sheet, im, 64, 0);
    pg_img_free(im);

    im = pg_img_new(32, 32, PG_RGB24(0x58BE62));                             /* leafy apple tree */
    for (int k = 0; k < 10; k++) pg_img_disc(im, rnd() * 32.0f, rnd() * 32.0f, 3.0f + rnd() * 4.0f, PG_ARGB(110, 40, 140, 76));
    for (int k = 0; k < 10; k++) pg_img_disc(im, rnd() * 32.0f, rnd() * 32.0f, 2.0f + rnd() * 3.0f, PG_ARGB(120, 150, 226, 110));
    for (int k = 0; k < 5; k++) {
        float x = 3.0f + rnd() * 26.0f, y = 5.0f + rnd() * 22.0f;
        pg_img_disc(im, x, y, 2.6f, PG_RGB24(0xF2444E));
        pg_img_disc(im, x - 0.7f, y - 0.8f, 0.9f, PG_RGB24(0xFFC0C0));
    }
    pg_img_noise(im, 5, 8);
    img_blit(sheet, im, 32, 0);
    pg_img_free(im);

    im = pg_img_new(48, 24, 0x00FFFFFFu);                                    /* three little flowers */
    static const uint32_t pet[3] = { 0xFF7AA8, 0xFFFFFF, 0xFFB03C };
    for (int f = 0; f < 3; f++) {
        float cx = 8.0f + (float)(f * 16);
        pg_img_line(im, cx, 12.0f, cx, 24.0f, 1.7f, PG_RGB24(0x4CAE55));
        for (int p = 0; p < 5; p++) {
            float a = (float)p * PG_TAU / 5.0f;
            pg_img_disc(im, cx + cosf(a) * 4.0f, 7.0f + sinf(a) * 4.0f, 3.0f, PG_RGB24(pet[f]));
        }
        pg_img_disc(im, cx, 7.0f, 2.4f, PG_RGB24(f == 2 ? 0xFFFFFF : 0xFFE04A));
    }
    img_blit(sheet, im, 0, 32);
    pg_img_free(im);
    PG_Tex all = tex_finish(sheet, true);
    t_tree_cone = tex_sub(all, 96, 64, 0, 0, 32, 32);
    t_tree_lolli = tex_sub(all, 96, 64, 32, 0, 32, 32);
    t_tree_trunk = tex_sub(all, 96, 64, 64, 0, 16, 16);
    for (int f = 0; f < 3; f++) t_flower[f] = tex_sub(all, 96, 64, f * 16, 32, 16, 24);
}

/* ------------------------------------------------------------------- meshes */
static PG_V3 seg_normal(int side, int i)               /* upward normal of hill segment i (xy plane) */
{
    float dx = hx(side, i + 1) - hx(side, i), dy = hy(side, i + 1) - hy(side, i), l = sqrtf(dx * dx + dy * dy);
    return pg_v3(-dy / l, dx / l, 0.0f);
}

static PG_V3 vert_normal(int side, int i)              /* smooth normal at outline point i */
{
    PG_V3 a = seg_normal(side, i > 0 ? i - 1 : 0), b = seg_normal(side, i < NH - 1 ? i : NH - 2);
    return pg_v3_norm(pg_v3_add(a, b));
}

static PG_Mesh *build_hill_top(int side)
{
    MeshB b = mb_begin((NH - 1) * 8, (NH - 1) * 12);
    const float lip = 8.0f;
    for (int s = 0; s + 1 < NH; s++) {
        float xa = hx(side, s), ya = hy(side, s), xb = hx(side, s + 1), yb = hy(side, s + 1);
        PG_V3 na = vert_normal(side, s), nb = vert_normal(side, s + 1);
        PG_V3 ca = pg_v3_norm(pg_v3(na.x * 0.55f, na.y * 0.55f, 0.83f)), cb = pg_v3_norm(pg_v3(nb.x * 0.55f, nb.y * 0.55f, 0.83f));
        PG_Color back = pg_rgb(196, 214, 196), front = pg_rgb(255, 255, 255), edge = pg_rgb(255, 255, 236);
        int a0 = mb_vert(&b, pg_v3(xa, ya, HILL_BACK_Z), na, 0, 0, back), a1 = mb_vert(&b, pg_v3(xb, yb, HILL_BACK_Z), nb, 1, 0, back);
        int a2 = mb_vert(&b, pg_v3(xb, yb, HILL_FRONT_Z - lip), nb, 1, 0.82f, front), a3 = mb_vert(&b, pg_v3(xa, ya, HILL_FRONT_Z - lip), na, 0, 0.82f, front);
        mb_quad(&b, a0, a1, a2, a3);
        int c0 = mb_vert(&b, pg_v3(xa, ya, HILL_FRONT_Z - lip), ca, 0, 0.82f, front), c1 = mb_vert(&b, pg_v3(xb, yb, HILL_FRONT_Z - lip), cb, 1, 0.82f, front);
        int c2 = mb_vert(&b, pg_v3(xb, yb - lip, HILL_FRONT_Z), cb, 1, 1.0f, edge), c3 = mb_vert(&b, pg_v3(xa, ya - lip, HILL_FRONT_Z), ca, 0, 1.0f, edge);
        mb_quad(&b, c0, c1, c2, c3);
    }
    return mb_end(&b);
}

static PG_Mesh *build_hill_front(int side)
{
    MeshB b = mb_begin((NH - 1) * 4, (NH - 1) * 6);
    const float lip = 8.0f, bottom = -300.0f;
    for (int s = 0; s + 1 < NH; s++) {
        float xa = hx(side, s), ya = hy(side, s) - lip, xb = hx(side, s + 1), yb = hy(side, s + 1) - lip;
        PG_V3 n = pg_v3(0, 0, 1);
        int a = mb_vert(&b, pg_v3(xa, ya, HILL_FRONT_Z), n, 0, 0, pg_rgb(255, 255, 255)), c = mb_vert(&b, pg_v3(xb, yb, HILL_FRONT_Z), n, 1, 0, pg_rgb(255, 255, 255));
        int d = mb_vert(&b, pg_v3(xb, bottom, HILL_FRONT_Z), n, 1, 1, pg_rgb(150, 160, 165)), e = mb_vert(&b, pg_v3(xa, bottom, HILL_FRONT_Z), n, 0, 1, pg_rgb(150, 160, 165));
        mb_quad(&b, a, c, d, e);
    }
    return mb_end(&b);
}

static PG_Mesh *build_wall(int side)                    /* side 0 = left wall facing +x, 1 = right wall facing -x */
{
    enum { NZ = 2, NY = 15 };
    const float y0 = -330.0f, y1 = 420.0f;
    float x = side ? WALL_IN_X : -WALL_IN_X, nx = side ? -1.0f : 1.0f;
    MeshB b = mb_begin(NZ * NY * 4, NZ * NY * 6);
    for (int j = 0; j < NY; j++)
        for (int i = 0; i < NZ; i++) {
            float za = lerpf(BACK_Z, FRONT_Z, (float)i / NZ), zb = lerpf(BACK_Z, FRONT_Z, (float)(i + 1) / NZ);
            float ya = lerpf(y0, y1, (float)j / NY), yb = lerpf(y0, y1, (float)(j + 1) / NY);
            float ka = 0.74f + 0.26f * (float)i / NZ, kb = 0.74f + 0.26f * (float)(i + 1) / NZ;   /* darker in the back corner */
            PG_V3 n = pg_v3(nx, 0, 0);
            int p0 = mb_vert(&b, pg_v3(x, ya, za), n, 0, 1, pg_rgb((uint8_t)(255 * ka), (uint8_t)(255 * ka), (uint8_t)(255 * ka)));
            int p1 = mb_vert(&b, pg_v3(x, ya, zb), n, 1, 1, pg_rgb((uint8_t)(255 * kb), (uint8_t)(255 * kb), (uint8_t)(255 * kb)));
            int p2 = mb_vert(&b, pg_v3(x, yb, zb), n, 1, 0, pg_rgb((uint8_t)(255 * kb), (uint8_t)(255 * kb), (uint8_t)(255 * kb)));
            int p3 = mb_vert(&b, pg_v3(x, yb, za), n, 0, 0, pg_rgb((uint8_t)(255 * ka), (uint8_t)(255 * ka), (uint8_t)(255 * ka)));
            mb_quad(&b, p0, p1, p2, p3);
        }
    return mb_end(&b);
}

/* --------------------------------------------------------------------- API */
void scene_init(void)
{
    paint_mural(); paint_quilt(); paint_hill(); paint_sky_sprites(); paint_trees();
    m_tile = pg_mesh_quad(TILE_W * PANEL_W / VW * 0.5f + 1.5f, TILE_H * PANEL_H / VH * 0.5f + 1.5f);
    for (int s = 0; s < 2; s++) {
        m_wall[s] = build_wall(s);
        m_hilltop[s] = build_hill_top(s);
        m_hillfront[s] = build_hill_front(s);
    }
    m_trunk = pg_mesh_cylinder(1.0f, 1.0f, 7);
    m_cone  = pg_mesh_cone(1.0f, 1.0f, 10);
    m_lolli = pg_mesh_sphere(1.0f, 10, 7);
    wall_bulge[0] = wall_bulge[1] = 0.0f;
}

void scene_shutdown(void)
{
    pg_mesh_free(m_tile); pg_mesh_free(m_trunk); pg_mesh_free(m_cone); pg_mesh_free(m_lolli);
    for (int s = 0; s < 2; s++) { pg_mesh_free(m_wall[s]); pg_mesh_free(m_hilltop[s]); pg_mesh_free(m_hillfront[s]); }
    m_tile = m_trunk = m_cone = m_lolli = NULL;
    for (int s = 0; s < 2; s++) m_wall[s] = m_hilltop[s] = m_hillfront[s] = NULL;
}

void scene_physics(PG_World *w)
{
    for (int side = 0; side < 2; side++)
        for (int i = 0; i + 1 < NH; i++) {
            PG_V3 n = seg_normal(side, i);
            PG_Body *b = pg_world_add_segment(w, hx(side, i) - n.x * HILL_T, hy(side, i) - n.y * HILL_T,
                                              hx(side, i + 1) - n.x * HILL_T, hy(side, i + 1) - n.y * HILL_T, HILL_T);
            if (!b) continue;
            b->friction = 0.30f; b->tag = TAG_HILL;
        }
    for (int side = 0; side < 2; side++) {
        float x = side ? WALL_IN_X + WALL_T : -(WALL_IN_X + WALL_T);
        PG_Body *b = pg_world_add_segment(w, x, -420.0f, x, 430.0f, WALL_T);
        if (!b) continue;
        b->friction = 0.05f; b->tag = TAG_WALL; b->user = &wall_id[side];
    }
    PG_Body *c = pg_world_add_segment(w, -560.0f, CEIL_Y + WALL_T, 560.0f, CEIL_Y + WALL_T, WALL_T);
    if (c) { c->friction = 0.05f; c->tag = TAG_WALL; c->user = &wall_id[2]; }
    PG_Body *f = pg_world_add_segment(w, -560.0f, -420.0f, 560.0f, -420.0f, WALL_T);      /* far below the screen: a safety net */
    if (f) { f->friction = 0.3f; f->tag = TAG_HILL; }
}

int scene_wall_side(const PG_Body *wall)
{
    if (!wall || wall->tag != TAG_WALL || !wall->user) return -1;
    return *(const int *)wall->user;
}

void scene_wall_hit(int side, float impact)
{
    if (side < 0 || side > 1) return;
    wall_bulge[side] = fmaxf(wall_bulge[side], clampf(impact / 700.0f, 0.25f, 1.0f));
}

void scene_update(void)
{
    t_now += PG_DT;
    for (int s = 0; s < 2; s++) wall_bulge[s] = decay(wall_bulge[s], 7.0f, PG_DT);
}

/* ------------------------------------------------------------------ drawing */
void scene_render_back(void)
{
    const unsigned fl = PG3D_BG | PG3D_UNLIT | PG3D_NOFOG;
    float t = t_now;
    float tw = TILE_W * PANEL_W / VW, th = TILE_H * PANEL_H / VH;
    for (int r = 0; r < 2; r++)
        for (int c = 0; c < 3; c++) {
            PG_M4 m = pg_m4_translate(-PANEL_W * 0.5f + tw * ((float)c + 0.5f), PANEL_H * 0.5f - th * ((float)r + 0.5f), BACK_Z);
            pg3d_mesh(m_tile, t_mural[r * 3 + c], &m, pg_rgb(255, 255, 255), fl);
        }
    /* the smiling sun, top left, with slowly turning rays */
    PG_V3 sun = pg_v3(-318.0f, 214.0f, BACK_Z + 10.0f);
    pg3d_sprite(pg3d_builtin(PG3D_TEX_GLOW), sun, 340.0f, 340.0f, 0, pg_rgba(255, 236, 150, 200), PG3D_BG);
    pg3d_sprite(t_sunrays, sun, 250.0f, 250.0f, t * 0.10f, pg_rgba(255, 255, 255, 235), PG3D_BG);
    pg3d_sprite(t_sunface, sun, 124.0f, 124.0f, sinf(t * 0.6f) * 0.06f, pg_rgb(255, 255, 255), PG3D_BG);
    /* drifting clouds: parallax by speed and size */
    static const struct { float x0, y, w, speed, z; } cl[5] = {
        { -420, 168, 250, 7.0f, -104 }, { 160, 226, 190, 5.0f, -108 }, { 420, 116, 280, 9.0f, -100 }, { -80, 252, 170, 4.0f, -110 }, { 640, 196, 220, 6.0f, -106 } };
    for (int i = 0; i < 5; i++) {
        float span = PANEL_W + cl[i].w * 2.0f;
        float x = fmodf(cl[i].x0 + t * cl[i].speed + PANEL_W * 0.5f + cl[i].w, span) - PANEL_W * 0.5f - cl[i].w;
        pg3d_sprite(t_cloudspr, pg_v3(x, cl[i].y, cl[i].z), cl[i].w, cl[i].w * 0.5f, 0, pg_rgba(255, 255, 255, 235), PG3D_BG);
    }
    /* cushion walls: left one pink, right one periwinkle; a hit makes the cushion bulge inwards for a moment */
    for (int s = 0; s < 2; s++) {
        PG_M4 m = pg_m4_translate((s ? -1.0f : 1.0f) * wall_bulge[s] * 9.0f, 0, 0);
        PG_Color tint = s ? pg_rgb(206, 214, 255) : pg_rgb(255, 208, 226);
        pg3d_mesh(m_wall[s], t_quilt, &m, tint, PG3D_BG | PG3D_UNLIT | PG3D_NOFOG);
    }
}

static void tree_cone(float x, float z, float s, float ph)
{
    float y = hill_surface(x < 0 ? 0 : 1, x);
    float sway = sinf(t_now * 1.1f + ph) * 0.035f;
    PG_M4 base = pg_m4_mul(pg_m4_translate(x, y, z), pg_m4_mul(pg_m4_rot_z(sway), pg_m4_scale(s, s, s)));
    PG_M4 m = pg_m4_mul(base, pg_m4_trs2(0, 10, 0, 0, 5.5f, 12.0f, 5.5f));
    pg3d_mesh(m_trunk, t_tree_trunk, &m, pg_rgb(255, 255, 255), 0);
    static const struct { float y, r, h; } cs[3] = { { 40, 34, 27 }, { 68, 26, 22 }, { 92, 17, 16 } };
    for (int i = 0; i < 3; i++) {
        m = pg_m4_mul(base, pg_m4_trs2(0, cs[i].y, 0, 0, cs[i].r, cs[i].h, cs[i].r));
        pg3d_mesh(m_cone, t_tree_cone, &m, pg_rgb(255, 255, 255), 0);
    }
}

static void tree_round(float x, float z, float s, float ph)
{
    float y = hill_surface(x < 0 ? 0 : 1, x);
    float sway = sinf(t_now * 0.9f + ph) * 0.035f;
    PG_M4 base = pg_m4_mul(pg_m4_translate(x, y, z), pg_m4_mul(pg_m4_rot_z(sway), pg_m4_scale(s, s, s)));
    PG_M4 m = pg_m4_mul(base, pg_m4_trs2(0, 28, 0, 0, 6.0f, 28.0f, 6.0f));
    pg3d_mesh(m_trunk, t_tree_trunk, &m, pg_rgb(255, 255, 255), 0);
    m = pg_m4_mul(base, pg_m4_trs2(0, 82, 0, 0, 36.0f, 31.0f, 36.0f));
    pg3d_mesh(m_lolli, t_tree_lolli, &m, pg_rgb(255, 255, 255), 0);
}

void scene_render_front(void)
{
    PG_M4 id = pg_m4_identity();
    for (int s = 0; s < 2; s++) {
        pg3d_mesh(m_hilltop[s], t_hilltop, &id, pg_rgb(255, 255, 255), 0);
        pg3d_mesh(m_hillfront[s], t_hillfront, &id, pg_rgb(255, 255, 255), 0);
    }
    tree_cone(-412.0f, -62.0f, 0.92f, 0.0f);
    tree_round(-318.0f, -84.0f, 0.86f, 1.7f);
    tree_round(322.0f, -84.0f, 0.9f, 3.1f);
    tree_cone(405.0f, -62.0f, 1.0f, 4.6f);
    /* little flowers on the hill tops, swaying */
    static const float fxs[] = { -440, -372, -330, -236, -214, 246, 268, 352, 380, 440 };
    for (int i = 0; i < 10; i++) {
        float x = fxs[i], y = hill_surface(x < 0 ? 0 : 1, x);
        float sway = sinf(t_now * 1.6f + (float)i * 1.3f) * 0.12f;
        pg3d_sprite(t_flower[i % 3], pg_v3(x, y + 13.0f, -30.0f - (float)(i % 3) * 12.0f), 18.0f, 27.0f, sway, pg_rgb(255, 255, 255), 0);
    }
}
