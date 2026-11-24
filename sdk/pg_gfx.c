/* pg_gfx.c - small anti-aliased 2D helpers for overlays, plus texture upload.
 * Everything is a textured quad of a pre-painted white mask, tinted with colour/alpha mod. */
#include "pg_internal.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static SDL_Texture *t_disc, *t_glow, *t_white, *t_font;

SDL_Texture *pg_texture_from_image(SDL_Renderer *r, const PG_Image *img, bool linear)
{
    SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, img->w, img->h);
    if (!t) pg_fatal("texture %dx%d: %s", img->w, img->h, SDL_GetError());
    SDL_UpdateTexture(t, NULL, img->px, img->w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, linear ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
    return t;
}

void pg_gfx_init(SDL_Renderer *r)
{
    PG_Image *im = pg_img_new(128, 128, 0x00FFFFFF);
    pg_img_disc(im, 64, 64, 63, 0xFFFFFFFF);
    t_disc = pg_texture_from_image(r, im, true);
    pg_img_free(im);

    im = pg_img_new(128, 128, 0x00FFFFFF);
    for (int j = 0; j < 128; j++)
        for (int i = 0; i < 128; i++) {
            float d = sqrtf(((float)i - 63.5f) * ((float)i - 63.5f) + ((float)j - 63.5f) * ((float)j - 63.5f)) / 64.0f;
            float a = d >= 1.0f ? 0.0f : (1.0f - d) * (1.0f - d);
            im->px[j * 128 + i] = PG_ARGB((uint32_t)(a * 255.0f), 255, 255, 255);
        }
    t_glow = pg_texture_from_image(r, im, true);
    SDL_SetTextureBlendMode(t_glow, SDL_BLENDMODE_ADD);
    pg_img_free(im);

    im = pg_img_new(4, 4, 0xFFFFFFFF);
    t_white = pg_texture_from_image(r, im, false);
    pg_img_free(im);

    im = pg_img_new(96, 48, 0x00FFFFFF);                     /* 16x6 cells of 6x8 */
    for (int c = 32; c < 127; c++) {
        const uint8_t *g = pg_font5x7(c);
        int cell = c - 32, ox = (cell % 16) * 6, oy = (cell / 16) * 8;
        for (int cx = 0; cx < 5; cx++) for (int cy = 0; cy < 7; cy++)
            if (g[cx] & (1 << cy)) im->px[(oy + cy) * 96 + ox + cx] = 0xFFFFFFFFu;
    }
    t_font = pg_texture_from_image(r, im, false);
    pg_img_free(im);
}

void pg_gfx_shutdown(void)
{
    if (t_disc) SDL_DestroyTexture(t_disc);
    if (t_glow) SDL_DestroyTexture(t_glow);
    if (t_white) SDL_DestroyTexture(t_white);
    if (t_font) SDL_DestroyTexture(t_font);
    t_disc = t_glow = t_white = t_font = NULL;
}

static void tint(SDL_Texture *t, PG_Color c) { SDL_SetTextureColorMod(t, c.r, c.g, c.b); SDL_SetTextureAlphaMod(t, c.a); }

void pg_disc(SDL_Renderer *r, float x, float y, float radius, PG_Color c)
{
    SDL_FRect d = { x - radius, y - radius, radius * 2, radius * 2 };
    tint(t_disc, c);
    SDL_RenderCopyF(r, t_disc, NULL, &d);
}

void pg_glow(SDL_Renderer *r, float x, float y, float radius, PG_Color c)
{
    SDL_FRect d = { x - radius, y - radius, radius * 2, radius * 2 };
    tint(t_glow, c);
    SDL_RenderCopyF(r, t_glow, NULL, &d);
}

void pg_ring(SDL_Renderer *r, float x, float y, float radius, float thick, PG_Color c)
{
    enum { N = 48 };
    SDL_Vertex v[N * 2 + 2];
    SDL_Color col = { c.r, c.g, c.b, c.a };
    float ro = radius + thick * 0.5f, ri = radius - thick * 0.5f;
    if (ri < 0) ri = 0;
    for (int i = 0; i <= N; i++) {
        float a = PG_TAU * (float)i / (float)N, cs = cosf(a), sn = sinf(a);
        v[i * 2].position = (SDL_FPoint){ x + cs * ro, y + sn * ro }; v[i * 2].color = col; v[i * 2].tex_coord = (SDL_FPoint){ 0, 0 };
        v[i * 2 + 1].position = (SDL_FPoint){ x + cs * ri, y + sn * ri }; v[i * 2 + 1].color = col; v[i * 2 + 1].tex_coord = (SDL_FPoint){ 0, 0 };
    }
    int idx[N * 6], k = 0;
    for (int i = 0; i < N; i++) {
        int a = i * 2, b = i * 2 + 1, cc = i * 2 + 2, d = i * 2 + 3;
        idx[k++] = a; idx[k++] = b; idx[k++] = cc; idx[k++] = cc; idx[k++] = b; idx[k++] = d;
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(r, NULL, v, N * 2 + 2, idx, N * 6);
}

void pg_rect(SDL_Renderer *r, float cx, float cy, float w, float h, float angle_rad, PG_Color c)
{
    SDL_FRect d = { cx - w * 0.5f, cy - h * 0.5f, w, h };
    tint(t_white, c);
    SDL_RenderCopyExF(r, t_white, NULL, &d, (double)angle_rad * 180.0 / PG_PI, NULL, SDL_FLIP_NONE);
}

void pg_line(SDL_Renderer *r, float x0, float y0, float x1, float y1, float thick, PG_Color c)
{
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    pg_rect(r, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f, len, thick, atan2f(dy, dx), c);
    pg_disc(r, x0, y0, thick * 0.5f, c);
    pg_disc(r, x1, y1, thick * 0.5f, c);
}

void pg_text(SDL_Renderer *r, float x, float y, int scale, PG_Color c, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (scale < 1) scale = 1;
    tint(t_font, c);
    for (const char *s = buf; *s; s++, x += 6.0f * (float)scale) {
        int ch = (unsigned char)*s;
        if (ch < 32 || ch > 126) continue;
        int cell = ch - 32;
        SDL_Rect src = { (cell % 16) * 6, (cell / 16) * 8, 5, 7 };
        SDL_FRect dst = { x, y, 5.0f * (float)scale, 7.0f * (float)scale };
        SDL_RenderCopyF(r, t_font, &src, &dst);
    }
}
