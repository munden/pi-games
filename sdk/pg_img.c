/* pg_img.c - CPU painter for procedural textures. See pg_img.h. */
#include "pg_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

PG_Image *pg_img_new(int w, int h, uint32_t fill)
{
    PG_Image *img = malloc(sizeof *img);
    if (!img) pg_fatal("out of memory (image)");
    img->w = w; img->h = h;
    img->px = malloc((size_t)w * (size_t)h * 4);
    if (!img->px) pg_fatal("out of memory (image %dx%d)", w, h);
    for (int i = 0; i < w * h; i++) img->px[i] = fill;
    return img;
}

void pg_img_free(PG_Image *img) { if (img) { free(img->px); free(img); } }

static inline float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

uint32_t pg_argb_lerp(uint32_t a, uint32_t b, float t)
{
    t = clamp01(t);
    uint32_t out = 0;
    for (int s = 0; s <= 24; s += 8) {
        float ca = (float)((a >> s) & 255), cb = (float)((b >> s) & 255);
        out |= (uint32_t)(ca + (cb - ca) * t + 0.5f) << s;
    }
    return out;
}

uint32_t pg_argb_scale(uint32_t c, float k)
{
    int r = (int)(((c >> 16) & 255) * k + 0.5f), g = (int)(((c >> 8) & 255) * k + 0.5f), b = (int)((c & 255) * k + 0.5f);
    r = r > 255 ? 255 : r; g = g > 255 ? 255 : g; b = b > 255 ? 255 : b;
    return (c & 0xFF000000u) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/* straight-alpha "over" with extra coverage */
static void over(uint32_t *d, uint32_t s, float cov)
{
    float sa = (float)(s >> 24) * (1.0f / 255.0f) * cov;
    if (sa <= 0.0f) return;
    uint32_t dv = *d;
    float da = (float)(dv >> 24) * (1.0f / 255.0f);
    float oa = sa + da * (1.0f - sa);
    uint32_t out = (uint32_t)(oa * 255.0f + 0.5f) << 24;
    for (int sh = 0; sh <= 16; sh += 8) {
        float sc = (float)((s >> sh) & 255), dc = (float)((dv >> sh) & 255);
        float oc = (sc * sa + dc * da * (1.0f - sa)) / oa;
        out |= (uint32_t)(oc + 0.5f) << sh;
    }
    *d = out;
}

void pg_img_rect(PG_Image *img, int x, int y, int w, int h, uint32_t argb)
{
    for (int j = y < 0 ? 0 : y; j < y + h && j < img->h; j++)
        for (int i = x < 0 ? 0 : x; i < x + w && i < img->w; i++) over(&img->px[j * img->w + i], argb, 1.0f);
}

void pg_img_frame(PG_Image *img, int x, int y, int w, int h, int t, uint32_t argb)
{
    pg_img_rect(img, x, y, w, t, argb);
    pg_img_rect(img, x, y + h - t, w, t, argb);
    pg_img_rect(img, x, y + t, t, h - 2 * t, argb);
    pg_img_rect(img, x + w - t, y + t, t, h - 2 * t, argb);
}

void pg_img_disc(PG_Image *img, float cx, float cy, float r, uint32_t argb)
{
    int x0 = (int)floorf(cx - r - 1), x1 = (int)ceilf(cx + r + 1), y0 = (int)floorf(cy - r - 1), y1 = (int)ceilf(cy + r + 1);
    for (int j = y0 < 0 ? 0 : y0; j <= y1 && j < img->h; j++)
        for (int i = x0 < 0 ? 0 : x0; i <= x1 && i < img->w; i++) {
            float d = sqrtf(((float)i + 0.5f - cx) * ((float)i + 0.5f - cx) + ((float)j + 0.5f - cy) * ((float)j + 0.5f - cy));
            over(&img->px[j * img->w + i], argb, clamp01(r + 0.5f - d));
        }
}

void pg_img_ring(PG_Image *img, float cx, float cy, float r, float thick, uint32_t argb)
{
    float ro = r + thick * 0.5f, ri = r - thick * 0.5f;
    int x0 = (int)floorf(cx - ro - 1), x1 = (int)ceilf(cx + ro + 1), y0 = (int)floorf(cy - ro - 1), y1 = (int)ceilf(cy + ro + 1);
    for (int j = y0 < 0 ? 0 : y0; j <= y1 && j < img->h; j++)
        for (int i = x0 < 0 ? 0 : x0; i <= x1 && i < img->w; i++) {
            float d = sqrtf(((float)i + 0.5f - cx) * ((float)i + 0.5f - cx) + ((float)j + 0.5f - cy) * ((float)j + 0.5f - cy));
            over(&img->px[j * img->w + i], argb, clamp01(ro + 0.5f - d) * clamp01(d - ri + 0.5f));
        }
}

void pg_img_line(PG_Image *img, float x0, float y0, float x1, float y1, float thick, uint32_t argb)
{
    float r = thick * 0.5f, dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
    int ix0 = (int)floorf((x0 < x1 ? x0 : x1) - r - 1), ix1 = (int)ceilf((x0 > x1 ? x0 : x1) + r + 1);
    int iy0 = (int)floorf((y0 < y1 ? y0 : y1) - r - 1), iy1 = (int)ceilf((y0 > y1 ? y0 : y1) + r + 1);
    for (int j = iy0 < 0 ? 0 : iy0; j <= iy1 && j < img->h; j++)
        for (int i = ix0 < 0 ? 0 : ix0; i <= ix1 && i < img->w; i++) {
            float px = (float)i + 0.5f - x0, py = (float)j + 0.5f - y0;
            float t = len2 > 0 ? clamp01((px * dx + py * dy) / len2) : 0;
            float ex = px - dx * t, ey = py - dy * t;
            over(&img->px[j * img->w + i], argb, clamp01(r + 0.5f - sqrtf(ex * ex + ey * ey)));
        }
}

void pg_img_star(PG_Image *img, float cx, float cy, float r_out, float r_in, int points, float rot, uint32_t argb)
{
    if (points < 3) points = 3;
    float vx[64], vy[64];
    int n = points * 2; if (n > 64) n = 64;
    for (int k = 0; k < n; k++) {
        float a = rot + (float)k * PG_PI / (float)points - PG_PI * 0.5f, rr = (k & 1) ? r_in : r_out;
        vx[k] = cx + cosf(a) * rr; vy[k] = cy + sinf(a) * rr;
    }
    int x0 = (int)floorf(cx - r_out - 1), x1 = (int)ceilf(cx + r_out + 1), y0 = (int)floorf(cy - r_out - 1), y1 = (int)ceilf(cy + r_out + 1);
    for (int j = y0 < 0 ? 0 : y0; j <= y1 && j < img->h; j++)
        for (int i = x0 < 0 ? 0 : x0; i <= x1 && i < img->w; i++) {
            int hit = 0;
            for (int sy = 0; sy < 4; sy++) for (int sx = 0; sx < 4; sx++) {
                float px = (float)i + ((float)sx + 0.5f) * 0.25f, py = (float)j + ((float)sy + 0.5f) * 0.25f;
                bool in = false;
                for (int a = 0, b = n - 1; a < n; b = a++)
                    if (((vy[a] > py) != (vy[b] > py)) && (px < (vx[b] - vx[a]) * (py - vy[a]) / (vy[b] - vy[a]) + vx[a])) in = !in;
                hit += in;
            }
            if (hit) over(&img->px[j * img->w + i], argb, (float)hit / 16.0f);
        }
}

void pg_img_gradient_v(PG_Image *img, int x, int y, int w, int h, uint32_t top, uint32_t bottom)
{
    for (int j = 0; j < h; j++) {
        uint32_t c = pg_argb_lerp(top, bottom, h > 1 ? (float)j / (float)(h - 1) : 0.0f);
        pg_img_rect(img, x, y + j, w, 1, c);
    }
}

void pg_img_gradient_radial(PG_Image *img, float cx, float cy, float r, uint32_t inner, uint32_t outer)
{
    for (int j = 0; j < img->h; j++)
        for (int i = 0; i < img->w; i++) {
            float d = sqrtf(((float)i + 0.5f - cx) * ((float)i + 0.5f - cx) + ((float)j + 0.5f - cy) * ((float)j + 0.5f - cy));
            if (d <= r) img->px[j * img->w + i] = pg_argb_lerp(inner, outer, d / r);
        }
}

void pg_img_checker(PG_Image *img, int cell, uint32_t a, uint32_t b)
{
    if (cell < 1) cell = 1;
    for (int j = 0; j < img->h; j++)
        for (int i = 0; i < img->w; i++) img->px[j * img->w + i] = (((i / cell) + (j / cell)) & 1) ? b : a;
}

void pg_img_stripes(PG_Image *img, int sw, int slant, uint32_t a, uint32_t b)
{
    if (sw < 1) sw = 1;
    for (int j = 0; j < img->h; j++)
        for (int i = 0; i < img->w; i++) {
            int k = i + (slant * j) / 8;
            img->px[j * img->w + i] = (((k % (2 * sw)) + 2 * sw) % (2 * sw) < sw) ? a : b;
        }
}

void pg_img_noise(PG_Image *img, int amount, uint32_t seed)
{
    uint32_t s = seed ? seed : 1u;
    for (int i = 0; i < img->w * img->h; i++) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        int d = (int)(s % (uint32_t)(2 * amount + 1)) - amount;
        uint32_t c = img->px[i];
        int r = (int)((c >> 16) & 255) + d, g = (int)((c >> 8) & 255) + d, b = (int)(c & 255) + d;
        r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; b = b < 0 ? 0 : b > 255 ? 255 : b;
        img->px[i] = (c & 0xFF000000u) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }
}

int pg_text_width_px(const char *s, int scale)
{
    int n = (int)strlen(s);
    return n ? n * 6 * scale - scale : 0;
}

void pg_img_text(PG_Image *img, int x, int y, int scale, uint32_t argb, const char *s)
{
    if (scale < 1) scale = 1;
    for (; *s; s++, x += 6 * scale) {
        const uint8_t *g = pg_font5x7((unsigned char)*s);
        if (!g) continue;
        for (int cx = 0; cx < 5; cx++)
            for (int cy = 0; cy < 7; cy++)
                if (g[cx] & (1 << cy)) pg_img_rect(img, x + cx * scale, y + cy * scale, scale, scale, argb);
    }
}

void pg_img_ps1(PG_Image *img)
{
    static const int8_t bayer[4][4] = { { -4, 0, -3, 1 }, { 2, -2, 3, -1 }, { -3, 1, -4, 0 }, { 3, -1, 2, -2 } };
    for (int j = 0; j < img->h; j++)
        for (int i = 0; i < img->w; i++) {
            uint32_t c = img->px[j * img->w + i];
            int off = bayer[j & 3][i & 3];
            uint32_t out = c & 0xFF000000u;
            for (int sh = 0; sh <= 16; sh += 8) {
                int v = (int)((c >> sh) & 255) + off;
                v = v < 0 ? 0 : v > 255 ? 255 : v;
                int q = v >> 3;
                out |= (uint32_t)((q << 3) | (q >> 2)) << sh;
            }
            img->px[j * img->w + i] = out;
        }
}
