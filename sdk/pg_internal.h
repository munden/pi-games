/* SDK-private. Games must not include this. */
#ifndef PG_INTERNAL_H
#define PG_INTERNAL_H
#include "pg.h"

void pg_audio_init(bool headless);
void pg_audio_shutdown(void);
void pg_gfx_init(SDL_Renderer *r);
void pg_gfx_shutdown(void);
void pg3d_init(SDL_Renderer *r);
void pg3d_shutdown(void);
const uint8_t *pg_font5x7(int ch);          /* 5 column bytes, bit0 = top row; NULL if unprintable */
#endif
