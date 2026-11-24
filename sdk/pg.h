/* pg.h - Pi Games SDK, API version 1.
 *
 * Platform: Raspberry Pi Zero 2 W, Raspberry Pi OS Lite, SDL2 on KMS/DRM,
 *           27" infrared multi-touch frame (10 contacts) on an easel, HDMI audio.
 * Audience: a 4-year-old.  Read AGENTS.md before using any of this.
 *
 * A game is a PG_Game struct plus:   int main(int c, char **v) { return pg_run(&game, c, v); }
 * The SDK owns SDL init, the window, the main loop, vsync, touch input, audio,
 * the parent gate (exit gesture), headless testing and screenshots.
 * Games must never call SDL_Init / SDL_CreateWindow / SDL_Quit / exit().
 *
 * Coordinates: the game draws into a fixed 960x540 "canvas" (PG_W x PG_H),
 * origin top-left, y down. The SDK scales it to the real screen (2x on 1080p).
 * On the 27" panel one canvas pixel is ~0.62 mm  =>  ~16 px per cm.
 */
#ifndef PG_SDK_H
#define PG_SDK_H

#include <SDL.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pg_version.h"
#include "pg_img.h"

#define PG_W        960
#define PG_H        540
#define PG_TICK_HZ  60
#define PG_DT       (1.0f / 60.0f)          /* update() always receives a fixed step */
#define PG_PI       3.14159265358979323846f
#define PG_TAU      6.28318530717958647692f
#define PG_PX_PER_CM 16.0f                   /* 27" 16:9 panel */

/* ------------------------------------------------------------------ colors */
typedef struct { uint8_t r, g, b, a; } PG_Color;

static inline PG_Color pg_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) { PG_Color c = { r, g, b, a }; return c; }
static inline PG_Color pg_rgb(uint8_t r, uint8_t g, uint8_t b)             { return pg_rgba(r, g, b, 255); }
static inline PG_Color pg_hex(uint32_t rgb) { return pg_rgb((uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb); }
static inline uint32_t pg_color_argb(PG_Color c) { return PG_ARGB(c.a, c.r, c.g, c.b); }
static inline PG_Color pg_alpha(PG_Color c, float a) { c.a = (uint8_t)(a <= 0 ? 0 : a >= 1 ? 255 : a * 255.0f); return c; }
PG_Color pg_hsv(float hue_deg, float sat, float val);        /* hue 0..360, sat/val 0..1 */
PG_Color pg_color_lerp(PG_Color a, PG_Color b, float t);

/* ------------------------------------------------------------------- game */
typedef struct PG_Game {
    const char *id;                      /* must equal the game.ini id and directory name */
    const char *title;
    int  (*init)(SDL_Renderer *r);       /* build meshes/textures/worlds. return 0 on success */
    void (*update)(void);                /* fixed 60 Hz simulation tick. Read pg_touches() here */
    void (*render)(SDL_Renderer *r);     /* draw one frame into the canvas (call pg3d_* then pg3d_end) */
    void (*shutdown)(void);              /* free everything you allocated (may be NULL) */
} PG_Game;

int      pg_run(const PG_Game *game, int argc, char **argv);   /* returns the process exit code */
void     pg_quit(void);                  /* ask the main loop to finish (parents' exit is automatic) */
uint64_t pg_ticks(void);                 /* update ticks since start (60 per second) */
float    pg_time(void);                  /* pg_ticks()/60 in seconds */
float    pg_frame_ms(void);              /* smoothed CPU ms per frame (update+render): degrade gracefully if > 12 */
SDL_Texture *pg_canvas(void);            /* the 960x540 render target; restore it with SDL_SetRenderTarget(r, pg_canvas())
                                            if render() ever draws into another texture */

void pg_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));    /* stderr; never per frame */
void pg_fatal(const char *fmt, ...) __attribute__((format(printf, 1, 2), noreturn));  /* unrecoverable init errors only */

/* ------------------------------------------------------------------ touch */
/* The only required input device. Up to 10 simultaneous contacts from the IR frame
 * (we track 16: palms and forearms create extra blobs - ignore what you cannot use).
 * Contacts are identified by `id` (0..15), stable while the finger stays down.
 *
 * Every contact is seen as DOWN for exactly one update tick, then HELD, then UP for
 * exactly one tick (even for a tap shorter than one tick).  The UP tick still carries
 * the final x,y.  Slots with state == PG_TOUCH_NONE are unused.
 * Never require: long-press, double-tap, pinch, swipes from the edge. Taps and drags only.
 */
#define PG_MAX_TOUCH 16
typedef enum { PG_TOUCH_NONE = 0, PG_TOUCH_DOWN, PG_TOUCH_HELD, PG_TOUCH_UP } PG_TouchState;

typedef struct PG_Touch {
    int           id;
    PG_TouchState state;
    float x, y;            /* canvas px (y down), clamped to the canvas */
    float px, py;          /* position on the previous tick */
    float vx, vy;          /* smoothed velocity, canvas px per second */
    float sx, sy;          /* where this contact started */
    int   ticks;           /* ticks since DOWN (0 on the DOWN tick) */
} PG_Touch;

const PG_Touch *pg_touches(void);        /* array of PG_MAX_TOUCH; valid only inside update()/render() */
int  pg_touch_count(void);               /* contacts with state != NONE this tick */
static inline bool pg_touch_active(const PG_Touch *t) { return t->state != PG_TOUCH_NONE; }
static inline bool pg_touch_in(const PG_Touch *t, float x, float y, float w, float h)
{ return t->x >= x && t->x < x + w && t->y >= y && t->y < y + h; }

/* ------------------------------------------------------------------ random */
void     pg_srand(uint32_t seed);
uint32_t pg_rand(void);
float    pg_randf(void);                          /* [0,1) */
float    pg_rand_range(float lo, float hi);       /* [lo,hi) */
int      pg_rand_int(int lo, int hi);             /* [lo,hi] inclusive */

/* ------------------------------------------------------------------- audio */
/* All sound is synthesised (no sample files). Output is soft-limited and the master
 * volume defaults to a child-safe level - do not try to make things louder.
 * The device is opened once and streams silence while idle, so the first sound after
 * a quiet period is not clipped by the HDMI sink. If there is no audio device every
 * call is a silent no-op: never depend on audio for gameplay. */
typedef enum { PG_WAVE_SINE, PG_WAVE_TRI, PG_WAVE_BELL, PG_WAVE_NOISE } PG_Wave;

/* One voice gliding f0 -> f1 Hz over `ms` milliseconds (for NOISE they are low-pass cutoffs).
 * vol 0..1. Starts with a ~4 ms attack and decays to silence, so it never clicks. */
void  pg_tone(PG_Wave wave, float f0_hz, float f1_hz, float ms, float vol);
float pg_note_hz(int step);                /* C-major pentatonic: 0=C4 1=D4 2=E4 3=G4 4=A4 5=C5 ... (negative ok).
                                              Any combination of these notes sounds pleasant together. */
/* Ready-made, rate-limited, gentle effects. */
void pg_sfx_pop(float vol);                /* balloon / bubble pop */
void pg_sfx_boing(float vol, float pitch); /* springy bounce; pitch ~1.0 */
void pg_sfx_thud(float vol);               /* soft low thump: blocks landing */
void pg_sfx_tap(float vol);                /* light wooden knock: balls touching */
void pg_sfx_whoosh(float vol);
void pg_sfx_chime(int pentatonic_step, float vol);   /* bell note */
void pg_sfx_squish(float vol, float pitch);          /* wet bloop for jelly */
void pg_sfx_sparkle(float vol);            /* quick rising twinkle */
void pg_audio_set_master(float v);         /* 0..1; default 0.5. Prefer leaving it alone. */

/* --------------------------------------------------------- 2D overlay helpers */
/* Anti-aliased 2D shapes for overlays and effects. For the main scene use pg3d.h:
 * flat 2D shapes alone do not meet the visual bar (see AGENTS.md "Look"). */
void pg_disc(SDL_Renderer *r, float x, float y, float radius, PG_Color c);
void pg_ring(SDL_Renderer *r, float x, float y, float radius, float thick, PG_Color c);
void pg_glow(SDL_Renderer *r, float x, float y, float radius, PG_Color c);        /* additive soft light */
void pg_line(SDL_Renderer *r, float x0, float y0, float x1, float y1, float thick, PG_Color c);
void pg_rect(SDL_Renderer *r, float cx, float cy, float w, float h, float angle_rad, PG_Color c);  /* centred, rotated */
void pg_text(SDL_Renderer *r, float x, float y, int scale, PG_Color c, const char *fmt, ...) __attribute__((format(printf, 6, 7)));
/* Upload a painted image as a texture (nearest or linear filtering). Caller destroys it. */
SDL_Texture *pg_texture_from_image(SDL_Renderer *r, const PG_Image *img, bool linear);

/* Command line understood by every game (see AGENTS.md, "Build and verify"):
 *   --headless          dummy audio/video, software renderer, fixed step, no sleeping, deterministic
 *   --frames N          stop after N ticks
 *   --touch-script F    inject touches from a script (format in docs/testing.md)
 *   --screenshot P.png  write the last canvas to P (png or bmp)
 *   --icon P.bmp        write a 256x256 launcher icon (centre of the canvas)
 *   --seed N            seed pg_rand
 *   --windowed / --fullscreen
 *   --fps               log performance once a second
 *   --calibrate         touch calibration for the parent, then exit
 *   --info              print video/audio/touch devices and exit
 */
#endif /* PG_SDK_H */
