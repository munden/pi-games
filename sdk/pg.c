/* pg.c - Pi Games SDK runtime: SDL setup, fixed-step loop, touch, headless mode, screenshots.
 *
 * Design notes
 *  - Every game draws into one fixed 960x540 render-target texture (the "canvas"). The
 *    SDK scales it to the real output (2x on 1080p, bilinear). This keeps physical sizes
 *    identical in every game and makes screenshots deterministic.
 *  - Touch is the only required input. Contacts are normalised, optionally corrected by
 *    a 2x3 affine calibration, mapped through the letterbox rect to canvas pixels and run
 *    through a small state machine that guarantees DOWN / HELD / UP are each seen by update().
 *  - --headless uses the dummy SDL drivers and a software renderer on a plain surface, runs
 *    exactly one tick per frame without sleeping and seeds the RNG, so make check is fast
 *    and reproducible on any build machine.
 */
#include "pg_internal.h"
#include "pg3d.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define GATE_BOX    96            /* px square in each top corner */
#define GATE_TICKS  120           /* hold both corners this long (2 s) to leave the game */
#define MOUSE_FINGER   ((int64_t)-1)
#define SCRIPT_FINGER  ((int64_t)1000000)

static struct {
    const PG_Game *game;
    SDL_Window    *win;
    SDL_Renderer  *ren;
    SDL_Texture   *canvas;
    SDL_Surface   *hsurf;               /* headless render surface */
    bool headless, fullscreen, windowed, show_fps, calibrate, info_only;
    bool vsync, mouse_touch, quit, kms, seed_set, evdev;
    uint32_t max_frames, seed;
    const char *script_path, *shot_path, *icon_path;
    uint64_t tick;
    int   out_w, out_h;
    SDL_FRect dest;
    float scale;
    float frame_ms;
    double work_acc_ms;                 /* update ms accumulated for the current frame */
    double stat_sum, stat_max, stat_upd, stat_3d; uint32_t stat_n; uint64_t stat_tris, stat_tris_max;
    int   gate_ticks;
    double fps_t0; uint32_t fps_frames; double fps_work;
} G;

/* ------------------------------------------------------------------ logging */
void pg_log(const char *fmt, ...)
{
    va_list ap;
    fputs("[pg] ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void pg_fatal(const char *fmt, ...)
{
    va_list ap;
    fputs("[pg] FATAL: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    if (SDL_WasInit(0)) SDL_Quit();
    exit(2);
}

static double now_s(void) { return (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency(); }

/* ------------------------------------------------------------------- random */
static uint32_t rng_state = 2463534242u;
void     pg_srand(uint32_t seed) { rng_state = seed ? seed : 0x9E3779B9u; for (int i = 0; i < 4; i++) (void)pg_rand(); }
uint32_t pg_rand(void) { uint32_t x = rng_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return rng_state = x; }
float    pg_randf(void) { return (float)(pg_rand() >> 8) * (1.0f / 16777216.0f); }
float    pg_rand_range(float lo, float hi) { return lo + (hi - lo) * pg_randf(); }
int      pg_rand_int(int lo, int hi) { return hi <= lo ? lo : lo + (int)(pg_rand() % (uint32_t)(hi - lo + 1)); }

/* ------------------------------------------------------------------- colors */
PG_Color pg_hsv(float h, float s, float v)
{
    h = fmodf(h, 360.0f); if (h < 0) h += 360.0f;
    float c = v * s, x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f)), m = v - c, r = 0, g = 0, b = 0;
    if      (h < 60)  { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else              { r = c; b = x; }
    return pg_rgb((uint8_t)((r + m) * 255.0f + 0.5f), (uint8_t)((g + m) * 255.0f + 0.5f), (uint8_t)((b + m) * 255.0f + 0.5f));
}

PG_Color pg_color_lerp(PG_Color a, PG_Color b, float t)
{
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return pg_rgba((uint8_t)(a.r + (b.r - a.r) * t + 0.5f), (uint8_t)(a.g + (b.g - a.g) * t + 0.5f),
                   (uint8_t)(a.b + (b.b - a.b) * t + 0.5f), (uint8_t)(a.a + (b.a - a.a) * t + 0.5f));
}

/* ------------------------------------------------------------ simple getters */
uint64_t     pg_ticks(void)     { return G.tick; }
float        pg_time(void)      { return (float)G.tick * PG_DT; }
float        pg_frame_ms(void)  { return G.frame_ms; }
void         pg_quit(void)      { G.quit = true; }
SDL_Texture *pg_canvas(void)    { return G.canvas; }

/* ====================================================================== touch */
typedef struct {
    bool    mapped;                 /* an SDL finger currently feeds this slot */
    int64_t finger;
    bool    down_pending, up_pending;
    float   rx, ry;                 /* latest canvas position */
    float   ddx, ddy;               /* device-normalised position at DOWN, before calibration (for --calibrate) */
} RawSlot;

static RawSlot  raw[PG_MAX_TOUCH];
static PG_Touch touch[PG_MAX_TOUCH];

/* 2x3 affine: device-normalised -> screen-normalised. Identity unless calibrated. */
static float cal[6] = { 1, 0, 0, 0, 1, 0 };
static bool  cal_on;
static char  cal_path[600];

static void cal_resolve_path(void)
{
    const char *e = getenv("PG_TOUCH_CAL");
    if (e && *e) { snprintf(cal_path, sizeof cal_path, "%s", e); return; }
    const char *x = getenv("XDG_CONFIG_HOME"), *h = getenv("HOME");
    if (x && *x)      snprintf(cal_path, sizeof cal_path, "%s/pi-games/touch.cal", x);
    else if (h && *h) snprintf(cal_path, sizeof cal_path, "%s/.config/pi-games/touch.cal", h);
    else              cal_path[0] = 0;
}

static bool cal_sane(const float *c)
{
    float det = c[0] * c[4] - c[1] * c[3];
    return fabsf(det) > 0.3f && fabsf(det) < 3.0f && fabsf(c[2]) < 1.5f && fabsf(c[5]) < 1.5f;
}

static void cal_load(void)
{
    cal_resolve_path();
    if (!cal_path[0]) return;
    FILE *f = fopen(cal_path, "r");
    if (!f) return;
    float c[6];
    int n = fscanf(f, "%f %f %f %f %f %f", &c[0], &c[1], &c[2], &c[3], &c[4], &c[5]);
    fclose(f);
    if (n == 6 && cal_sane(c)) { memcpy(cal, c, sizeof cal); cal_on = true; pg_log("touch calibration loaded from %s", cal_path); }
    else pg_log("ignoring bad touch calibration file %s", cal_path);
}

static bool cal_save(const float *c)
{
    if (!cal_path[0]) return false;
    char tmp[620];
    snprintf(tmp, sizeof tmp, "%s", cal_path);
    for (char *p = tmp + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    FILE *f = fopen(cal_path, "w");
    if (!f) return false;
    fprintf(f, "%.6f %.6f %.6f %.6f %.6f %.6f\n", c[0], c[1], c[2], c[3], c[4], c[5]);
    return fclose(f) == 0;
}

static void norm_to_canvas(float nx, float ny, bool use_cal, float *ox, float *oy)
{
    if (use_cal && cal_on) {
        float ax = cal[0] * nx + cal[1] * ny + cal[2];
        float ay = cal[3] * nx + cal[4] * ny + cal[5];
        nx = ax; ny = ay;
    }
    float cx = (nx * (float)G.out_w - G.dest.x) / G.scale;
    float cy = (ny * (float)G.out_h - G.dest.y) / G.scale;
    cx = cx < 0 ? 0 : cx > PG_W - 1 ? PG_W - 1 : cx;
    cy = cy < 0 ? 0 : cy > PG_H - 1 ? PG_H - 1 : cy;
    *ox = cx; *oy = cy;
}

static int slot_find(int64_t finger)
{
    for (int i = 0; i < PG_MAX_TOUCH; i++) if (raw[i].mapped && raw[i].finger == finger) return i;
    return -1;
}

static int slot_alloc(void)
{
    for (int i = 0; i < PG_MAX_TOUCH; i++)
        if (!raw[i].mapped && !raw[i].down_pending && !raw[i].up_pending && touch[i].state == PG_TOUCH_NONE) return i;
    return -1;
}

static void touch_down(int64_t finger, float nx, float ny, bool use_cal)
{
    if (slot_find(finger) >= 0) return;
    int i = slot_alloc();
    if (i < 0) return;                       /* more than 16 blobs: ignore the rest */
    RawSlot *s = &raw[i];
    s->mapped = true; s->finger = finger; s->down_pending = true; s->ddx = nx; s->ddy = ny;
    norm_to_canvas(nx, ny, use_cal, &s->rx, &s->ry);
}

static void touch_move(int64_t finger, float nx, float ny, bool use_cal)
{
    int i = slot_find(finger);
    if (i >= 0) norm_to_canvas(nx, ny, use_cal, &raw[i].rx, &raw[i].ry);
}

static void touch_up(int64_t finger, float nx, float ny, bool use_cal)
{
    int i = slot_find(finger);
    if (i < 0) return;
    norm_to_canvas(nx, ny, use_cal, &raw[i].rx, &raw[i].ry);
    raw[i].mapped = false;
    raw[i].up_pending = true;
}

/* Called once at the start of every update tick. */
static void touch_tick_begin(void)
{
    for (int i = 0; i < PG_MAX_TOUCH; i++) {
        PG_Touch *t = &touch[i];
        RawSlot  *s = &raw[i];
        t->px = t->x; t->py = t->y;
        if (t->state == PG_TOUCH_UP)        t->state = PG_TOUCH_NONE;
        else if (t->state == PG_TOUCH_DOWN) t->state = PG_TOUCH_HELD;

        if (t->state == PG_TOUCH_HELD) {
            t->x = s->rx; t->y = s->ry;
            t->ticks++;
            if (s->up_pending) { t->state = PG_TOUCH_UP; s->up_pending = false; }
            float vx = (t->x - t->px) / PG_DT, vy = (t->y - t->py) / PG_DT;
            t->vx = 0.6f * t->vx + 0.4f * vx;
            t->vy = 0.6f * t->vy + 0.4f * vy;
        } else if (t->state == PG_TOUCH_NONE && s->down_pending) {
            s->down_pending = false;
            t->id = i; t->state = PG_TOUCH_DOWN; t->ticks = 0;
            t->x = t->px = t->sx = s->rx;
            t->y = t->py = t->sy = s->ry;
            t->vx = t->vy = 0;
        }
    }
}

const PG_Touch *pg_touches(void) { return touch; }
int pg_touch_count(void)
{
    int n = 0;
    for (int i = 0; i < PG_MAX_TOUCH; i++) if (touch[i].state != PG_TOUCH_NONE) n++;
    return n;
}

/* ============================================================== touch script */
enum { EV_DOWN, EV_MOVE, EV_UP };
typedef struct { uint32_t tick, order; int slot, type; float x, y; } ScriptEv;
static ScriptEv *sev; static size_t sev_n, sev_cap, sev_next;

static void sev_add(uint32_t tick, int slot, int type, float x, float y)
{
    if (sev_n == sev_cap) {
        sev_cap = sev_cap ? sev_cap * 2 : 1024;
        sev = realloc(sev, sev_cap * sizeof *sev);
        if (!sev) pg_fatal("out of memory (touch script)");
    }
    sev[sev_n] = (ScriptEv){ tick, (uint32_t)sev_n, slot, type, x, y };
    sev_n++;
}

static int sev_cmp(const void *a, const void *b)
{
    const ScriptEv *x = a, *y = b;
    if (x->tick != y->tick) return x->tick < y->tick ? -1 : 1;
    return x->order < y->order ? -1 : x->order > y->order;
}

static void script_drag(uint32_t t, int dur, int slot, float x0, float y0, float x1, float y1)
{
    if (dur < 2) dur = 2;
    sev_add(t, slot, EV_DOWN, x0, y0);
    for (int k = 1; k < dur; k++) {
        float f = (float)k / (float)dur;
        sev_add(t + (uint32_t)k, slot, EV_MOVE, x0 + (x1 - x0) * f, y0 + (y1 - y0) * f);
    }
    sev_add(t + (uint32_t)dur, slot, EV_UP, x1, y1);
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static void script_load(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) pg_fatal("cannot open touch script %s", path);
    char line[256];
    int ln = 0;
    while (fgets(line, sizeof line, f)) {
        ln++;
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        char cmd[16];
        if (sscanf(line, "%15s", cmd) != 1) continue;
        unsigned t; int d, s, n; float a, b, c, e, g, h;
        const char *args = line + strcspn(line, " \t");
        if (!strcmp(cmd, "tap") && sscanf(args, "%u %d %f %f", &t, &s, &a, &b) == 4) {
            sev_add(t, s, EV_DOWN, a, b); sev_add(t + 4, s, EV_UP, a, b);
        } else if (!strcmp(cmd, "hold") && sscanf(args, "%u %d %d %f %f", &t, &d, &s, &a, &b) == 5) {
            sev_add(t, s, EV_DOWN, a, b); sev_add(t + (uint32_t)(d < 2 ? 2 : d), s, EV_UP, a, b);
        } else if (!strcmp(cmd, "drag") && sscanf(args, "%u %d %d %f %f %f %f", &t, &d, &s, &a, &b, &c, &e) == 7) {
            script_drag(t, d, s, a, b, c, e);
        } else if (!strcmp(cmd, "wave") && sscanf(args, "%u %d %d %f %f %f %f", &t, &d, &s, &a, &b, &c, &e) == 7) {
            if (d < 2) d = 2;
            sev_add(t, s, EV_DOWN, a + c, b);
            for (int k = 1; k < d; k++) {
                float ang = PG_TAU * e * (float)k / (float)d;
                sev_add(t + (uint32_t)k, s, EV_MOVE, a + c * cosf(ang), b + c * sinf(ang));
            }
            sev_add(t + (uint32_t)d, s, EV_UP, a + c * cosf(PG_TAU * e), b + c * sinf(PG_TAU * e));
        } else if (!strcmp(cmd, "down") && sscanf(args, "%u %d %f %f", &t, &s, &a, &b) == 4) {
            sev_add(t, s, EV_DOWN, a, b);
        } else if (!strcmp(cmd, "move") && sscanf(args, "%u %d %f %f", &t, &s, &a, &b) == 4) {
            sev_add(t, s, EV_MOVE, a, b);
        } else if (!strcmp(cmd, "up") && sscanf(args, "%u %d %f %f", &t, &s, &a, &b) == 4) {
            sev_add(t, s, EV_UP, a, b);
        } else if (!strcmp(cmd, "fuzz") && sscanf(args, "%u %d %d", &t, &d, &n) == 3) {
            /* n independent fingers making random drags for d ticks; private LCG so the game RNG is untouched */
            uint32_t st = 12345u + t * 7919u + (uint32_t)n * 104729u;
            #define LCG() (st = st * 1664525u + 1013904223u, (st >> 8) & 0xFFFF)
            for (int i = 0; i < n && i < PG_MAX_TOUCH; i++) {
                uint32_t tt = t + LCG() % 20u;
                while (tt < t + (uint32_t)d) {
                    int dur = 8 + (int)(LCG() % 50u);
                    g = 40 + (float)(LCG() % (PG_W - 80)); h = 40 + (float)(LCG() % (PG_H - 80));
                    float x1 = clampf(g + ((float)(LCG() % 400) - 200.0f), 10, PG_W - 10);
                    float y1 = clampf(h + ((float)(LCG() % 300) - 150.0f), 10, PG_H - 10);
                    script_drag(tt, dur, i, g, h, x1, y1);
                    tt += (uint32_t)dur + 2u + LCG() % 25u;
                }
            }
            #undef LCG
        } else {
            fclose(f);
            pg_fatal("touch script %s line %d: cannot parse '%s'", path, ln, cmd);
        }
    }
    fclose(f);
    qsort(sev, sev_n, sizeof *sev, sev_cmp);
    sev_next = 0;
    pg_log("touch script: %zu events", sev_n);
}

static void script_apply(uint64_t tick)
{
    while (sev_next < sev_n && sev[sev_next].tick <= tick) {
        const ScriptEv *e = &sev[sev_next++];
        float nx = e->x / (float)PG_W, ny = e->y / (float)PG_H;
        int64_t fid = SCRIPT_FINGER + e->slot;
        if (e->type == EV_DOWN) touch_down(fid, nx, ny, true);
        else if (e->type == EV_MOVE) touch_move(fid, nx, ny, true);
        else touch_up(fid, nx, ny, true);
    }
}

/* ========================================================= events and window */
static void update_metrics(void)
{
    if (G.win && !G.headless) SDL_GetRendererOutputSize(G.ren, &G.out_w, &G.out_h);
    else { G.out_w = PG_W; G.out_h = PG_H; }
    float sx = (float)G.out_w / PG_W, sy = (float)G.out_h / PG_H;
    G.scale = sx < sy ? sx : sy;
    G.dest.w = PG_W * G.scale; G.dest.h = PG_H * G.scale;
    G.dest.x = ((float)G.out_w - G.dest.w) * 0.5f;
    G.dest.y = ((float)G.out_h - G.dest.h) * 0.5f;
}

/* fallback touch source (pg_evdev.c): same pipeline as SDL finger events */
static void evdev_inject(int kind, int64_t id, float nx, float ny)
{
    int64_t fid = id + 5000000;
    if (kind == PG_INJECT_DOWN) touch_down(fid, nx, ny, true);
    else if (kind == PG_INJECT_MOVE) touch_move(fid, nx, ny, true);
    else touch_up(fid, nx, ny, true);
}

static void pump_events(void)
{
    if (G.evdev) {
        if (SDL_GetNumTouchDevices() > 0) { pg_evdev_stop(evdev_inject); G.evdev = false; pg_log("SDL now sees a touch device; native evdev fallback off"); }
        else pg_evdev_poll(evdev_inject);
    }
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT: G.quit = true; break;
        case SDL_KEYDOWN:
            if (ev.key.keysym.sym == SDLK_ESCAPE) G.quit = true;
            break;
        case SDL_FINGERDOWN:   touch_down(ev.tfinger.fingerId, ev.tfinger.x, ev.tfinger.y, true); break;
        case SDL_FINGERMOTION: touch_move(ev.tfinger.fingerId, ev.tfinger.x, ev.tfinger.y, true); break;
        case SDL_FINGERUP:     touch_up(ev.tfinger.fingerId, ev.tfinger.x, ev.tfinger.y, true); break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        case SDL_MOUSEMOTION: {
            if (!G.mouse_touch || ev.motion.which == SDL_TOUCH_MOUSEID) break;
            int ww = PG_W, wh = PG_H;
            if (G.win) SDL_GetWindowSize(G.win, &ww, &wh);
            float x = (ev.type == SDL_MOUSEMOTION ? ev.motion.x : ev.button.x) / (float)ww;
            float y = (ev.type == SDL_MOUSEMOTION ? ev.motion.y : ev.button.y) / (float)wh;
            if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) touch_down(MOUSE_FINGER, x, y, false);
            else if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) touch_up(MOUSE_FINGER, x, y, false);
            else if (ev.type == SDL_MOUSEMOTION && (ev.motion.state & SDL_BUTTON_LMASK)) touch_move(MOUSE_FINGER, x, y, false);
            break;
        }
        default: break;
        }
    }
}

/* ============================================================== parent gate */
static void gate_update(void)
{
    bool tl = false, tr = false;
    for (int i = 0; i < PG_MAX_TOUCH; i++) {
        const PG_Touch *t = &touch[i];
        if (t->state != PG_TOUCH_DOWN && t->state != PG_TOUCH_HELD) continue;
        if (t->y < GATE_BOX && t->x < GATE_BOX) tl = true;
        if (t->y < GATE_BOX && t->x > PG_W - GATE_BOX) tr = true;
    }
    if (tl && tr) { if (++G.gate_ticks >= GATE_TICKS) { pg_log("parent gate: leaving"); G.quit = true; } }
    else if (G.gate_ticks > 0) G.gate_ticks -= 4, G.gate_ticks = G.gate_ticks < 0 ? 0 : G.gate_ticks;
}

static void gate_draw(SDL_Renderer *r)
{
    if (G.gate_ticks < 20) return;
    float p = (float)G.gate_ticks / (float)GATE_TICKS;
    PG_Color c = pg_rgba(255, 255, 255, 170);
    pg_ring(r, 48, 48, 14 + 34 * p, 5, c);
    pg_ring(r, PG_W - 48, 48, 14 + 34 * p, 5, c);
}

/* =============================================================== PNG output */
static uint32_t crc_tab[256];
static void crc_init(void)
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_tab[n] = c;
    }
}
static uint32_t crc_update(uint32_t crc, const uint8_t *d, size_t n)
{
    crc = ~crc;
    while (n--) crc = crc_tab[(crc ^ *d++) & 255] ^ (crc >> 8);
    return ~crc;
}
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void png_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
    uint8_t hdr[8]; put32(hdr, len); memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    uint32_t crc = crc_update(0, (const uint8_t *)type, 4);
    crc = crc_update(crc, data, len);
    uint8_t c4[4]; put32(c4, crc); fwrite(c4, 1, 4, f);
}

/* Uncompressed (stored-block) PNG: big but dependency-free; screenshots are not committed. */
static bool write_png(const char *path, const uint8_t *rgb, int w, int h)
{
    crc_init();
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13]; put32(ihdr, (uint32_t)w); put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    png_chunk(f, "IHDR", ihdr, 13);
    size_t rowlen = (size_t)w * 3 + 1, rawlen = rowlen * (size_t)h;
    uint8_t *raw_data = malloc(rawlen);
    if (!raw_data) { fclose(f); return false; }
    for (int y = 0; y < h; y++) { raw_data[y * rowlen] = 0; memcpy(raw_data + y * rowlen + 1, rgb + (size_t)y * w * 3, (size_t)w * 3); }
    size_t nblk = (rawlen + 65534) / 65535;
    size_t zlen = 2 + rawlen + nblk * 5 + 4;
    uint8_t *z = malloc(zlen);
    if (!z) { free(raw_data); fclose(f); return false; }
    size_t zp = 0; z[zp++] = 0x78; z[zp++] = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t off = 0; off < rawlen; off += 65535) {
        size_t n = rawlen - off < 65535 ? rawlen - off : 65535;
        z[zp++] = (off + n >= rawlen) ? 1 : 0;
        z[zp++] = (uint8_t)(n & 255); z[zp++] = (uint8_t)(n >> 8);
        z[zp++] = (uint8_t)(~n & 255); z[zp++] = (uint8_t)((~n >> 8) & 255);
        memcpy(z + zp, raw_data + off, n); zp += n;
    }
    for (size_t i = 0; i < rawlen; i++) { a = (a + raw_data[i]) % 65521u; b = (b + a) % 65521u; }
    put32(z + zp, (b << 16) | a); zp += 4;
    png_chunk(f, "IDAT", z, (uint32_t)zp);
    png_chunk(f, "IEND", NULL, 0);
    free(z); free(raw_data);
    return fclose(f) == 0;
}

/* Read the canvas (or another target) as tightly packed RGB. */
static uint8_t *capture_rgb(SDL_Texture *target, int w, int h)
{
    uint32_t *px = malloc((size_t)w * h * 4);
    uint8_t  *rgb = malloc((size_t)w * h * 3);
    if (!px || !rgb) { free(px); free(rgb); return NULL; }
    SDL_SetRenderTarget(G.ren, target);
    if (SDL_RenderReadPixels(G.ren, NULL, SDL_PIXELFORMAT_ARGB8888, px, w * 4) != 0) {
        pg_log("screenshot: SDL_RenderReadPixels failed: %s", SDL_GetError());
        free(px); free(rgb); return NULL;
    }
    for (int i = 0; i < w * h; i++) { rgb[i * 3] = (uint8_t)(px[i] >> 16); rgb[i * 3 + 1] = (uint8_t)(px[i] >> 8); rgb[i * 3 + 2] = (uint8_t)px[i]; }
    free(px);
    return rgb;
}

static bool ends_with(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf);
    if (a < b) return false;
    for (size_t i = 0; i < b; i++) if (tolower((unsigned char)s[a - b + i]) != suf[i]) return false;
    return true;
}

static void save_rgb(const char *path, uint8_t *rgb, int w, int h)
{
    bool ok;
    if (ends_with(path, ".bmp")) {
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(rgb, w, h, 24, w * 3, SDL_PIXELFORMAT_RGB24);
        ok = s && SDL_SaveBMP(s, path) == 0;
        if (s) SDL_FreeSurface(s);
    } else ok = write_png(path, rgb, w, h);
    if (ok) pg_log("wrote %s", path); else pg_log("could not write %s", path);
}

static void write_outputs(void)
{
    if (G.shot_path) {
        uint8_t *rgb = capture_rgb(G.canvas, PG_W, PG_H);
        if (rgb) { save_rgb(G.shot_path, rgb, PG_W, PG_H); free(rgb); }
    }
    if (G.icon_path) {
        SDL_Texture *icon = SDL_CreateTexture(G.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 256, 256);
        if (icon) {
            SDL_SetRenderTarget(G.ren, icon);
            SDL_Rect src = { (PG_W - PG_H) / 2, 0, PG_H, PG_H };
            SDL_RenderCopy(G.ren, G.canvas, &src, NULL);
            uint8_t *rgb = capture_rgb(icon, 256, 256);
            if (rgb) { save_rgb(G.icon_path, rgb, 256, 256); free(rgb); }
            SDL_SetRenderTarget(G.ren, NULL);
            SDL_DestroyTexture(icon);
        }
    }
}

/* ================================================================ calibration */
static int   cal_step;
static float cal_u[4], cal_v[4];
static int   cal_msg_ticks;          /* >0: showing result, then quit */
static bool  cal_ok;
static const float cal_tx[4] = { 0.10f, 0.90f, 0.10f, 0.90f };
static const float cal_ty[4] = { 0.12f, 0.12f, 0.88f, 0.88f };

static bool solve3(double A[3][4], double out[3])
{
    for (int c = 0; c < 3; c++) {
        int piv = c;
        for (int r = c + 1; r < 3; r++) if (fabs(A[r][c]) > fabs(A[piv][c])) piv = r;
        if (fabs(A[piv][c]) < 1e-9) return false;
        if (piv != c) for (int k = 0; k < 4; k++) { double t = A[c][k]; A[c][k] = A[piv][k]; A[piv][k] = t; }
        for (int r = 0; r < 3; r++) {
            if (r == c) continue;
            double f = A[r][c] / A[c][c];
            for (int k = c; k < 4; k++) A[r][k] -= f * A[c][k];
        }
    }
    for (int i = 0; i < 3; i++) out[i] = A[i][3] / A[i][i];
    return true;
}

/* Least-squares affine fit: device (u,v) -> screen (x,y), x = a*u + b*v + c. */
static bool fit_affine(const float *u, const float *v, const float *x, const float *y, int n, float *out)
{
    double M[3][4] = {{0}}, N[3][4] = {{0}}, ox[3], oy[3];
    for (int i = 0; i < n; i++) {
        double r[3] = { u[i], v[i], 1.0 };
        for (int a = 0; a < 3; a++) {
            for (int b = 0; b < 3; b++) { M[a][b] += r[a] * r[b]; N[a][b] += r[a] * r[b]; }
            M[a][3] += r[a] * x[i];
            N[a][3] += r[a] * y[i];
        }
    }
    if (!solve3(M, ox) || !solve3(N, oy)) return false;
    for (int i = 0; i < 3; i++) { out[i] = (float)ox[i]; out[3 + i] = (float)oy[i]; }
    return true;
}

static int calib_init(SDL_Renderer *r) { (void)r; cal_on = false; cal_step = 0; cal_msg_ticks = 0; return 0; }

static void calib_update(void)
{
    if (cal_msg_ticks > 0) { if (--cal_msg_ticks == 0) { if (cal_ok) pg_quit(); else cal_step = 0; } return; }
    for (int i = 0; i < PG_MAX_TOUCH; i++) {
        if (touch[i].state != PG_TOUCH_DOWN || cal_step >= 4) continue;
        cal_u[cal_step] = raw[i].ddx; cal_v[cal_step] = raw[i].ddy;
        pg_sfx_chime(cal_step + 2, 0.5f);
        cal_step++;
        break;
    }
    if (cal_step == 4) {
        float out[6];
        cal_ok = fit_affine(cal_u, cal_v, cal_tx, cal_ty, 4, out) && cal_sane(out);
        if (cal_ok) {
            cal_ok = cal_save(out);
            if (cal_ok) pg_log("calibration saved to %s", cal_path);
            else pg_log("could not save calibration to %s", cal_path);
        } else pg_log("calibration result looks wrong - try again");
        cal_msg_ticks = 120;
        cal_step = 4;
    }
}

static void calib_render(SDL_Renderer *r)
{
    SDL_SetRenderDrawColor(r, 18, 22, 48, 255);
    SDL_RenderClear(r);
    if (cal_msg_ticks > 0) {
        const char *m = cal_ok ? "SAVED. THANK YOU!" : "THAT LOOKED WRONG. TRY AGAIN.";
        pg_text(r, (PG_W - pg_text_width_px(m, 4)) / 2.0f, PG_H / 2.0f - 14, 4, pg_rgb(255, 255, 255), "%s", m);
        return;
    }
    if (cal_step < 4) {
        float x, y;
        norm_to_canvas(cal_tx[cal_step], cal_ty[cal_step], false, &x, &y);
        float pulse = 1.0f + 0.12f * sinf(pg_time() * 6.0f);
        pg_glow(r, x, y, 90 * pulse, pg_rgb(80, 160, 255));
        pg_ring(r, x, y, 38 * pulse, 6, pg_rgb(255, 255, 255));
        pg_disc(r, x, y, 10, pg_rgb(255, 220, 80));
        char buf[48]; snprintf(buf, sizeof buf, "TOUCH THE CIRCLE  %d/4", cal_step + 1);
        pg_text(r, (PG_W - pg_text_width_px(buf, 3)) / 2.0f, PG_H / 2.0f - 10, 3, pg_rgb(200, 215, 255), "%s", buf);
        pg_text(r, (PG_W - pg_text_width_px("PRESS ESC TO CANCEL", 2)) / 2.0f, PG_H / 2.0f + 30, 2, pg_rgb(120, 130, 170), "PRESS ESC TO CANCEL");
    }
}

static const PG_Game calib_game = { "calibrate", "Touch calibration", calib_init, calib_update, calib_render, NULL };

/* ============================================================== diagnostics */
static int do_info(void)
{
    SDL_version v; SDL_GetVersion(&v);
    printf("SDL %d.%d.%d (built against %d.%d.%d)\n", v.major, v.minor, v.patch, SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);
    if (!getenv("SDL_VIDEODRIVER") && !getenv("DISPLAY") && !getenv("WAYLAND_DISPLAY")) SDL_setenv("SDL_VIDEODRIVER", "kmsdrm", 0);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) { printf("SDL_Init failed: %s\n", SDL_GetError()); return 1; }
    printf("video driver: %s\n", SDL_GetCurrentVideoDriver());
    for (int d = 0; d < SDL_GetNumVideoDisplays(); d++) {
        SDL_DisplayMode m;
        const char *nm = SDL_GetDisplayName(d);
        if (SDL_GetDesktopDisplayMode(d, &m) == 0) printf("display %d: %s  %dx%d @%dHz  (%d modes)\n", d, nm ? nm : "?", m.w, m.h, m.refresh_rate, SDL_GetNumDisplayModes(d));
    }
    printf("audio driver: %s\n", SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "(none)");
    for (int i = 0; i < SDL_GetNumAudioDevices(0); i++) printf("audio out %d: %s\n", i, SDL_GetAudioDeviceName(i, 0));
    int nt = SDL_GetNumTouchDevices();
    printf("touch devices: %d\n", nt);
    for (int i = 0; i < nt; i++) { const char *n = SDL_GetTouchName(i); printf("  touch %d: %s\n", i, n ? n : "?"); }
    pg_evdev_start();
    int ne = pg_evdev_count();
    printf("native evdev touch devices (fallback reader): %d\n", ne);
    pg_evdev_stop(NULL);
    if (nt == 0 && ne == 0) printf("  (none found: check the USB cable and that this user is in the 'input' group)\n");
    SDL_Quit();
    return 0;
}

/* ================================================================ main loop */
static void usage(const char *prog)
{
    fprintf(stderr,
        "usage: %s [options]\n"
        "  --headless          dummy audio/video, software renderer, deterministic\n"
        "  --frames N          stop after N update ticks\n"
        "  --touch-script F    inject touches from script F\n"
        "  --screenshot P      write last frame to P (.png or .bmp)\n"
        "  --icon P.bmp        write a 256x256 launcher icon\n"
        "  --seed N            seed the random generator\n"
        "  --windowed | --fullscreen\n"
        "  --fps               log performance every 2 s\n"
        "  --calibrate         run touch calibration (for the grown-up)\n"
        "  --info              list video, audio and touch devices\n", prog);
}

static bool parse_args(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        #define NEED() do { if (i + 1 >= argc) { fprintf(stderr, "%s needs a value\n", a); return false; } } while (0)
        if (!strcmp(a, "--headless")) G.headless = true;
        else if (!strcmp(a, "--frames"))       { NEED(); G.max_frames = (uint32_t)strtoul(argv[++i], NULL, 10); }
        else if (!strcmp(a, "--touch-script")) { NEED(); G.script_path = argv[++i]; }
        else if (!strcmp(a, "--screenshot"))   { NEED(); G.shot_path = argv[++i]; }
        else if (!strcmp(a, "--icon"))         { NEED(); G.icon_path = argv[++i]; }
        else if (!strcmp(a, "--seed"))         { NEED(); G.seed = (uint32_t)strtoul(argv[++i], NULL, 10); G.seed_set = true; }
        else if (!strcmp(a, "--windowed"))     G.windowed = true;
        else if (!strcmp(a, "--fullscreen"))   G.fullscreen = true;
        else if (!strcmp(a, "--fps"))          G.show_fps = true;
        else if (!strcmp(a, "--calibrate"))    G.calibrate = true;
        else if (!strcmp(a, "--info"))         G.info_only = true;
        else { fprintf(stderr, "unknown option %s\n", a); return false; }
        #undef NEED
    }
    return true;
}

static bool init_video(const char *title)
{
    if (G.headless) {
        SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
        SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    } else if (!getenv("SDL_VIDEODRIVER") && !getenv("DISPLAY") && !getenv("WAYLAND_DISPLAY")) {
        SDL_setenv("SDL_VIDEODRIVER", "kmsdrm", 0);        /* Pi OS Lite: no desktop, draw straight to the display */
    }
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");         /* we use finger events only */
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) { pg_log("SDL_Init failed: %s", SDL_GetError()); return false; }
    const char *drv = SDL_GetCurrentVideoDriver();
    G.kms = drv && !strcmp(drv, "kmsdrm");
    const char *mt = getenv("PG_MOUSE_TOUCH");
    G.mouse_touch = mt ? atoi(mt) != 0 : !G.kms && !G.headless;   /* desktop dev: mouse acts as a finger */

    if (G.headless) {
        G.hsurf = SDL_CreateRGBSurfaceWithFormat(0, PG_W, PG_H, 32, SDL_PIXELFORMAT_ARGB8888);
        if (!G.hsurf) { pg_log("surface: %s", SDL_GetError()); return false; }
        G.ren = SDL_CreateSoftwareRenderer(G.hsurf);
    } else {
        Uint32 wf = SDL_WINDOW_SHOWN;
        if ((G.kms || G.fullscreen) && !G.windowed) wf |= SDL_WINDOW_FULLSCREEN_DESKTOP; else wf |= SDL_WINDOW_RESIZABLE;
        G.win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, PG_W, PG_H, wf);
        if (!G.win) { pg_log("window: %s", SDL_GetError()); return false; }
        if (G.kms && !getenv("SDL_RENDER_DRIVER")) SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengles2");
        G.ren = SDL_CreateRenderer(G.win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!G.ren) {
            pg_log("accelerated renderer unavailable (%s); using software", SDL_GetError());
            G.ren = SDL_CreateRenderer(G.win, -1, SDL_RENDERER_SOFTWARE);
        }
        if (G.kms || G.fullscreen) SDL_ShowCursor(SDL_DISABLE);
    }
    if (!G.ren) { pg_log("renderer: %s", SDL_GetError()); return false; }

    SDL_RendererInfo info;
    SDL_GetRendererInfo(G.ren, &info);
    G.vsync = (info.flags & SDL_RENDERER_PRESENTVSYNC) != 0;
    if (!(info.flags & SDL_RENDERER_TARGETTEXTURE)) { pg_log("renderer %s cannot render to textures", info.name); return false; }
    pg_log("video=%s renderer=%s vsync=%d headless=%d", drv ? drv : "?", info.name, G.vsync, G.headless);

    G.canvas = SDL_CreateTexture(G.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, PG_W, PG_H);
    if (!G.canvas) { pg_log("canvas: %s", SDL_GetError()); return false; }
    SDL_SetTextureScaleMode(G.canvas, SDL_ScaleModeLinear);
    update_metrics();
    return true;
}

static void tick_once(const PG_Game *g)
{
    script_apply(G.tick);
    touch_tick_begin();
    gate_update();
    double t0 = now_s();
    g->update();
    G.work_acc_ms += (now_s() - t0) * 1000.0;
    G.tick++;
}

static void render_frame(const PG_Game *g)
{
    update_metrics();
    double t0 = now_s();
    SDL_SetRenderTarget(G.ren, G.canvas);
    SDL_SetRenderDrawBlendMode(G.ren, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(G.ren, 0, 0, 0, 255);
    SDL_RenderClear(G.ren);
    g->render(G.ren);
    SDL_SetRenderTarget(G.ren, G.canvas);
    gate_draw(G.ren);
    if (G.show_fps && !G.headless) pg_text(G.ren, 8, PG_H - 18, 2, pg_rgb(255, 255, 0), "%.1f MS", G.frame_ms);
    double upd_ms = G.work_acc_ms;
    double work = G.work_acc_ms + (now_s() - t0) * 1000.0;
    G.work_acc_ms = 0;
    G.stat_upd += upd_ms; G.stat_3d += pg3d_cpu_ms();
    { int tr = pg3d_last_tris(); G.stat_tris += (uint64_t)tr; if ((uint64_t)tr > G.stat_tris_max) G.stat_tris_max = (uint64_t)tr; }
    G.frame_ms = G.frame_ms == 0 ? (float)work : G.frame_ms * 0.9f + (float)work * 0.1f;
    G.stat_sum += work; if (work > G.stat_max) G.stat_max = work; G.stat_n++;

    if (!G.headless) {
        SDL_SetRenderTarget(G.ren, NULL);
        SDL_SetRenderDrawColor(G.ren, 0, 0, 0, 255);
        SDL_RenderClear(G.ren);
        SDL_RenderCopyF(G.ren, G.canvas, NULL, &G.dest);
        SDL_RenderPresent(G.ren);
    }
    if (G.show_fps) {
        G.fps_frames++; G.fps_work += work;
        double t = now_s();
        if (G.fps_t0 == 0) G.fps_t0 = t;
        if (t - G.fps_t0 >= 2.0) {
            pg_log("fps=%.1f work=%.2fms touches=%d", G.fps_frames / (t - G.fps_t0), G.fps_work / G.fps_frames, pg_touch_count());
            G.fps_t0 = t; G.fps_frames = 0; G.fps_work = 0;
        }
    }
}

static void main_loop(const PG_Game *g)
{
    double last = now_s(), acc = 0;
    while (!G.quit) {
        pump_events();
        if (G.quit) break;
        int steps = 1;
        if (!G.headless) {
            double t = now_s();
            acc += t - last; last = t;
            if (acc > 0.1) acc = 0.1;                     /* after a stall, do not fast-forward */
            steps = (int)(acc / PG_DT);
            acc -= steps * PG_DT;
            if (steps == 0) { SDL_Delay(1); continue; }
        }
        for (int i = 0; i < steps; i++) tick_once(g);
        render_frame(g);
        if (G.max_frames && G.tick >= G.max_frames) break;
    }
}

int pg_run(const PG_Game *game, int argc, char **argv)
{
    memset(&G, 0, sizeof G);
    G.game = game;
    if (!game || !game->update || !game->render) { fprintf(stderr, "pg_run: game needs update and render\n"); return 64; }
    if (!parse_args(argc, argv)) { usage(argv[0]); return 64; }
    if (G.info_only) return do_info();

    pg_srand(G.seed_set ? G.seed : G.headless ? 1u : (uint32_t)time(NULL) ^ (uint32_t)SDL_GetTicks());
    cal_load();
    if (G.script_path && !G.headless && G.max_frames == 0) pg_log("note: touch script without --frames runs until you quit");

    const PG_Game *g = G.calibrate ? &calib_game : game;
    if (!init_video(g->title ? g->title : "pi-games")) { if (SDL_WasInit(0)) SDL_Quit(); return 1; }
    pg_gfx_init(G.ren);
    pg3d_init(G.ren);
    pg_audio_init(G.headless);
    if (!G.headless && !getenv("PG_NO_EVDEV") && SDL_GetNumTouchDevices() == 0) {
        pg_log("SDL reports no touch device; trying to read /dev/input directly (set PG_NO_EVDEV=1 to disable)");
        pg_evdev_start();
        G.evdev = true;
    }
    if (G.script_path) script_load(G.script_path);

    int rc = 0;
    SDL_SetRenderTarget(G.ren, G.canvas);
    if (g->init && g->init(G.ren) != 0) { pg_log("%s: init failed", g->id ? g->id : "game"); rc = 2; }
    else {
        main_loop(g);
        write_outputs();
        if (G.max_frames && G.stat_n)
            pg_log("done: ticks=%llu avg_work=%.2fms max_work=%.2fms update=%.2fms cpu3d=%.2fms tris_avg=%llu tris_max=%llu (this machine; "
                   "work includes software rasterising; the Pi Zero 2 W runs update+cpu3d roughly 8x slower)",
                   (unsigned long long)G.tick, G.stat_sum / G.stat_n, G.stat_max, G.stat_upd / G.stat_n, G.stat_3d / G.stat_n,
                   (unsigned long long)(G.stat_tris / G.stat_n), (unsigned long long)G.stat_tris_max);
        if (g->shutdown) g->shutdown();
    }

    if (G.evdev) pg_evdev_stop(NULL);
    pg_audio_shutdown();
    pg3d_shutdown();
    pg_gfx_shutdown();
    if (G.canvas) SDL_DestroyTexture(G.canvas);
    if (G.ren) SDL_DestroyRenderer(G.ren);
    if (G.win) SDL_DestroyWindow(G.win);
    if (G.hsurf) SDL_FreeSurface(G.hsurf);
    SDL_Quit();
    free(sev); sev = NULL; sev_n = sev_cap = 0;
    return rc;
}
