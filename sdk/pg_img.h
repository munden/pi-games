/* pg_img.h - tiny CPU bitmap painter used to generate textures procedurally.
 *
 * Games ship NO image files: every texture is painted at init() with these
 * helpers, then handed to pg3d_texture(). Pixels are 0xAARRGGBB (straight alpha).
 * Keep textures small (16..128 px square): that is the PS1 look and it is cheap.
 */
#ifndef PG_IMG_H
#define PG_IMG_H

#include <stdint.h>

typedef struct PG_Image {
    int w, h;
    uint32_t *px;                 /* w*h pixels, row-major, 0xAARRGGBB */
} PG_Image;

#define PG_ARGB(a, r, g, b) ((uint32_t)(((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b)))
#define PG_RGB24(rgb)       (0xFF000000u | ((uint32_t)(rgb) & 0xFFFFFFu))     /* PG_RGB24(0xFF8800) */

PG_Image *pg_img_new(int w, int h, uint32_t fill);
void      pg_img_free(PG_Image *img);

uint32_t  pg_argb_lerp(uint32_t a, uint32_t b, float t);
uint32_t  pg_argb_scale(uint32_t c, float k);              /* multiply rgb by k (clamped) */

void pg_img_rect(PG_Image *img, int x, int y, int w, int h, uint32_t argb);
void pg_img_frame(PG_Image *img, int x, int y, int w, int h, int thick, uint32_t argb);
void pg_img_disc(PG_Image *img, float cx, float cy, float r, uint32_t argb);              /* anti-aliased */
void pg_img_ring(PG_Image *img, float cx, float cy, float r, float thick, uint32_t argb);
void pg_img_line(PG_Image *img, float x0, float y0, float x1, float y1, float thick, uint32_t argb);
void pg_img_star(PG_Image *img, float cx, float cy, float r_out, float r_in, int points, float rot, uint32_t argb);
void pg_img_gradient_v(PG_Image *img, int x, int y, int w, int h, uint32_t top, uint32_t bottom);
void pg_img_gradient_radial(PG_Image *img, float cx, float cy, float r, uint32_t inner, uint32_t outer);
void pg_img_checker(PG_Image *img, int cell, uint32_t a, uint32_t b);
void pg_img_stripes(PG_Image *img, int stripe_w, int slant, uint32_t a, uint32_t b);      /* slant: px shift per row */
void pg_img_noise(PG_Image *img, int amount, uint32_t seed);   /* +-amount (0..255) luminance grain */
void pg_img_text(PG_Image *img, int x, int y, int scale, uint32_t argb, const char *s);   /* 5x7 font, scale>=1 */
int  pg_text_width_px(const char *s, int scale);
/* Quantise to 5 bits/channel with 4x4 ordered dithering - the PS1 15-bit look. */
void pg_img_ps1(PG_Image *img);

#endif
