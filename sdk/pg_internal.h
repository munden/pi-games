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
const uint8_t *pg_font5x7(int ch);
/* fallback touch reader (pg_evdev.c) */
enum { PG_INJECT_DOWN, PG_INJECT_MOVE, PG_INJECT_UP };
typedef void (*PG_InjectFn)(int kind, int64_t finger_id, float nx, float ny);
void pg_evdev_start(void);
void pg_evdev_poll(PG_InjectFn inject);
void pg_evdev_stop(PG_InjectFn inject);
int  pg_evdev_count(void);
const uint8_t *pg_font5x7_unused_(void);          /* 5 column bytes, bit0 = top row; NULL if unprintable */
#endif
