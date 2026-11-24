/* sand.c - falling-sand automaton: simulation, trapdoor, lighting and texture upload.
 *
 * Grid:  two byte arrays (cell id, fall speed) with a padded stride of 256 so a cell is (y<<8)+x.
 *        Array row 0 is the ceiling, rows 1..SD_H the box (last SD_PLANK rows = floor planks),
 *        rows SD_H+1, SD_H+2 are the hidden drain; padding cells are walls so neighbours never need bounds checks.
 * Rules: every grain only ever moves to a LOWER row, so one pass bottom-to-top never moves a grain twice.
 *        Free fall accelerates 1,2,3 cells/tick; a resting grain slides diagonally when both the side and the
 *        diagonal cell are empty (never through a diagonal wall); sometimes it hops two columns, which flattens
 *        piles from 45 degrees towards a natural ~35.
 * Speed: 16x16 chunks stay "active" for a few ticks after anything near them changed; idle chunks cost one byte read.
 */
#include "sand.h"
#include "pg.h"

#include <math.h>
#include <string.h>

#define PW        256
#define PH        128
#define CHK       16
#define CW        (PW / CHK)
#define CHN       (PH / CHK)
#define ROW_FIRST 1
#define ROW_LAST  SD_H
#define SINK0     (SD_H + 1)
#define SINK1     (SD_H + 2)
#define PLANK0    (SD_H - SD_PLANK + 1)
#define FALL_MAX  3
#define ACT_HOLD  4

#define N_SAND    (SD_HUES * 4)          /* sand ids 1..N_SAND */
#define ID_STATIC 160
enum { ID_WALL = ID_STATIC, ID_PLANK, ID_PEG0, ID_PEG7 = ID_PEG0 + 7, ID_WEDGE, ID_FUNNEL, ID_LAST };

#define SHADOW_PX PG_ARGB(92, 36, 28, 96)

static uint8_t  g[PW * PH];              /* cell ids */
static uint8_t  v[PW * PH];              /* fall speed of sand grains, 0 = resting */
static uint32_t px[SD_W * SD_H];         /* the lit texture */
static uint32_t spx[SD_W * SD_H];        /* pre-painted pixels of static cells */
static uint8_t  act[CW * CHN], dirty[CW * CHN];
static uint32_t rng = 0x2545F491u, tick;
static int      n_sand, capacity, ceil_count;
static uint32_t sand_lut[N_SAND + 1][3]; /* [id][0 dark, 1 normal, 2 lit] */
static SDL_Texture *tex;

static struct {
    int   state;
    float xc, r, r_target, rate;
    int   hold, hold_min;
    float close_fill;
} door;
static int drained_n; static uint8_t drained_h[16]; static uint8_t drained_x[16];

static double st_sim_ms, st_sim_max, st_light_ms; static long st_ticks, st_frames, st_cells;

static inline uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static inline double now_ms(void) { return (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency(); }

/* ----------------------------------------------------------- chunk bookkeeping */
static inline void mark(int x, int y)
{
    int cx = x >> 4, cy = y >> 4, lx = x & 15, ly = y & 15, c = cy * CW + cx;
    act[c] = ACT_HOLD; dirty[c] = 1;
    if (lx == 0 && cx > 0)        { act[c - 1] = ACT_HOLD; dirty[c - 1] = 1; if (ly == 0 && cy > 0) { act[c - CW - 1] = ACT_HOLD; dirty[c - CW - 1] = 1; } if (ly == 15 && cy < CHN - 1) { act[c + CW - 1] = ACT_HOLD; dirty[c + CW - 1] = 1; } }
    else if (lx == 15 && cx < CW - 1) { act[c + 1] = ACT_HOLD; dirty[c + 1] = 1; if (ly == 0 && cy > 0) { act[c - CW + 1] = ACT_HOLD; dirty[c - CW + 1] = 1; } if (ly == 15 && cy < CHN - 1) { act[c + CW + 1] = ACT_HOLD; dirty[c + CW + 1] = 1; } }
    if (ly == 0 && cy > 0)             { act[c - CW] = ACT_HOLD; dirty[c - CW] = 1; }
    else if (ly == 15 && cy < CHN - 1) { act[c + CW] = ACT_HOLD; dirty[c + CW] = 1; }
}

/* ------------------------------------------------------------------ palette */
static PG_Color hue_color(int h)
{
    /* toy-candy rainbow: saturated, and the muddy yellow-greens / dark blues are nudged to stay bright */
    float deg = (float)h * (360.0f / (float)SD_HUES) + 3.0f;
    PG_Color c = pg_hsv(deg, 0.70f, 1.0f);
    float lum = 0.30f * c.r + 0.59f * c.g + 0.11f * c.b;               /* perceived brightness 0..255 */
    float k = 1.0f + (190.0f - lum) * 0.0030f;                          /* lift darks, tame the brightest */
    float r = c.r * k, gg = c.g * k, b = c.b * k;
    c.r = (uint8_t)(r > 255 ? 255 : r); c.g = (uint8_t)(gg > 255 ? 255 : gg); c.b = (uint8_t)(b > 255 ? 255 : b);
    return c;
}

uint32_t sand_hue_argb(int hue)
{
    PG_Color c = hue_color(((hue % SD_HUES) + SD_HUES) % SD_HUES);
    return PG_ARGB(255, c.r, c.g, c.b);
}

static void build_sand_lut(void)
{
    static const float vm[4] = { 0.88f, 0.96f, 1.03f, 1.10f };
    static const float sm[3] = { 0.68f, 1.0f, 1.16f };
    for (int h = 0; h < SD_HUES; h++) {
        PG_Color b = hue_color(h);
        for (int s = 0; s < 4; s++)
            for (int k = 0; k < 3; k++) {
                float m = vm[s] * sm[k], mix = (k == 2) ? 0.20f : 0.0f;
                float rr = b.r * m, gg = b.g * m, bb = b.b * m;
                if (k == 0) { rr *= 0.92f; bb = bb * 1.05f + 6.0f; }    /* shaded side leans cool */
                rr += (255.0f - rr) * mix; gg += (255.0f - gg) * mix; bb += (255.0f - bb) * mix;
                sand_lut[1 + h * 4 + s][k] = PG_ARGB(255, rr > 255 ? 255 : (uint32_t)rr, gg > 255 ? 255 : (uint32_t)gg, bb > 255 ? 255 : (uint32_t)bb);
            }
    }
}

/* ------------------------------------------------------------------- level */
static void put(int x, int y, uint8_t id)               /* x,y: box coordinates, 0-based, y down */
{
    if (x < 0 || x >= SD_W || y < 0 || y >= SD_H) return;
    g[((y + 1) << 8) + x + 1] = id;
}

static void diamond(int cx, int cy, int r, uint8_t id)
{
    for (int dy = -r; dy <= r; dy++) {
        int w = r - (dy < 0 ? -dy : dy);
        for (int dx = -w; dx <= w; dx++) put(cx + dx, cy + dy, id);
    }
}

static void build_level(void)
{
    memset(g, 0, sizeof g); memset(v, 0, sizeof v);
    for (int y = 0; y < PH; y++) for (int x = 0; x < PW; x++)
        if (y == 0 || y > SINK1 || x == 0 || x > SD_W) g[(y << 8) + x] = ID_WALL;
    /* trapdoor floor */
    for (int y = SD_H - SD_PLANK; y < SD_H; y++) for (int x = 0; x < SD_W; x++) put(x, y, ID_PLANK);
    /* the idle funnel, top centre: a 45 degree bowl, four cells thick, and a short neck five cells wide */
    const int fx = SD_W / 2;
    for (int t = 0; t < 25; t++) for (int k = 0; k < 4; k++) { put(fx - 30 + t + k, 4 + t, ID_FUNNEL); put(fx + 27 - t + k, 4 + t, ID_FUNNEL); }
    for (int y = 29; y < 35; y++) for (int k = 0; k < 4; k++) { put(fx - 6 + k, y, ID_FUNNEL); put(fx + 3 + k, y, ID_FUNNEL); }
    /* plinko pegs */
    static const int row1[3] = { 56, 114, 172 }, row2[4] = { 27, 85, 143, 201 };
    for (int i = 0; i < 3; i++) diamond(row1[i], 56, 6, (uint8_t)(ID_PEG0 + (i * 3) % 8));
    for (int i = 0; i < 4; i++) diamond(row2[i], 82, 6, (uint8_t)(ID_PEG0 + (i * 3 + 1) % 8));
    /* steep corner ramps (2 rows per column, steeper than sand can rest on) that feed the floor */
    for (int y = 82; y < SD_H - SD_PLANK; y++) {
        for (int x = 0; x <= (y - 82) / 2; x++) { put(x, y, ID_WEDGE); put(SD_W - 1 - x, y, ID_WEDGE); }
    }
}

/* --------------------------------------------------- painting static cells */
static uint32_t rgb_scale(uint32_t c, float k)
{
    float r = (float)((c >> 16) & 255) * k, gg = (float)((c >> 8) & 255) * k, b = (float)(c & 255) * k;
    return PG_ARGB(255, r > 255 ? 255 : (uint32_t)r, gg > 255 ? 255 : (uint32_t)gg, b > 255 ? 255 : (uint32_t)b);
}

static uint32_t hash2(int x, int y) { uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u; h = (h ^ (h >> 13)) * 1274126177u; return h ^ (h >> 16); }

static void paint_static(void)
{
    static const uint32_t peg_col[8] = { 0xF2544E, 0xFF9A2E, 0xFFD23A, 0x58C95A, 0x2CC4B8, 0x4A8DF0, 0x9A62E8, 0xF26EB8 };
    memset(spx, 0, sizeof spx);
    for (int y = 1; y <= SD_H; y++)
        for (int x = 1; x <= SD_W; x++) {
            int i = (y << 8) + x; uint8_t id = g[i];
            if (id < ID_PLANK || id >= ID_LAST) continue;
            uint32_t base;
            float f = 1.0f;
            /* how deep inside the shape are we, looking toward the light (up-left) and away from it (down-right)? */
            int ul = 0, lr = 0;
            while (ul < 6 && g[i - (ul + 1) * (PW + 1)] == id) ul++;
            while (lr < 3 && g[i + (lr + 1) * (PW + 1)] == id) lr++;
            switch (id) {
            case ID_PLANK: {
                base = 0xC98B4E;
                int col = (x - 1) % 41;
                float grain = (float)(hash2(x >> 2, y) & 15) * 0.012f;
                f = 0.92f + grain + ((y - PLANK0) == 0 ? 0.20f : (y - PLANK0) == 2 ? -0.20f : 0.0f);
                if (col == 0 || col == 1) f *= 0.62f;                            /* plank seams */
                else if ((hash2(x, y >> 1) & 63) == 0) f *= 0.8f;
                break; }
            case ID_WEDGE:
                base = (((x + y) >> 2) & 1) ? 0xFF8F3F : 0xFFC247; f = 1.0f; break;
            case ID_FUNNEL:
                base = ((y >> 2) & 1) ? 0xF2544E : 0xFFF6F0; f = 1.0f; break;
            default:
                base = peg_col[id - ID_PEG0]; break;
            }
            if (id != ID_PLANK) f = 1.18f - 0.055f * (float)ul - (lr == 0 ? 0.20f : 0.0f);
            spx[(y - 1) * SD_W + (x - 1)] = rgb_scale(base, f);
        }
}

/* ------------------------------------------------------------------ init */
void sand_stats_reset(void) { st_sim_ms = st_sim_max = st_light_ms = 0; st_ticks = st_frames = st_cells = 0; }

static void count_capacity(void)
{
    capacity = 0;
    for (int y = ROW_FIRST; y < PLANK0; y++) for (int x = 1; x <= SD_W; x++) if (g[(y << 8) + x] == 0) capacity++;
}

void sand_reset(void)
{
    build_level(); paint_static(); count_capacity();
    memset(act, 0, sizeof act); memset(dirty, 1, sizeof dirty);
    n_sand = 0; ceil_count = 0; drained_n = 0;
    memset(&door, 0, sizeof door);
}

void sand_init(SDL_Renderer *r, uint32_t seed)
{
    rng = seed ? seed : 0x2545F491u;
    build_sand_lut();
    sand_reset();
    sand_stats_reset();
    tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SD_W, SD_H);
    if (!tex) pg_fatal("sand texture: %s", SDL_GetError());
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
    memset(px, 0, sizeof px);
    SDL_UpdateTexture(tex, NULL, px, SD_W * 4);
}

void sand_shutdown(void)
{
    if (st_ticks > 0)
        pg_log("sand-pour: sim %.3f ms/tick avg (%.3f max), %.0f cells/tick visited, relight %.3f ms/frame avg (this machine)",
               st_sim_ms / (double)st_ticks, st_sim_max, (double)st_cells / (double)st_ticks, st_frames ? st_light_ms / (double)st_frames : 0.0);
    if (tex) SDL_DestroyTexture(tex);
    tex = NULL;
}

/* ------------------------------------------------------------- spawning */
static inline int spawn_cell(int ax, int ay, int id)
{
    int i = (ay << 8) + ax;
    if (g[i]) return 0;
    g[i] = (uint8_t)id; v[i] = 0; n_sand++;
    mark(ax, ay);
    return 1;
}

int sand_pour(float x0, float y0, float x1, float y1, int hue, int n)
{
    int made = 0;
    hue = ((hue % SD_HUES) + SD_HUES) % SD_HUES;
    for (int k = 0; k < n; k++) {
        float t = ((float)k + (float)(rnd() & 255) * (1.0f / 256.0f)) / (float)n;
        int cx = (int)(x0 + (x1 - x0) * t) + (int)(rnd() % 5u) - 2;
        int cy = (int)(y0 + (y1 - y0) * t) + (int)(rnd() % 3u);
        cx = cx < 0 ? 0 : cx >= SD_W ? SD_W - 1 : cx;
        cy = cy < 1 ? 1 : cy >= SD_H - SD_PLANK ? SD_H - SD_PLANK - 1 : cy;
        int id = 1 + hue * 4 + (int)(rnd() & 3u);
        /* if the spot is taken (pile or peg) nudge up a cell or two: a nozzle that is buried simply stops pouring */
        for (int up = 0; up < 3 && cy - up >= 1; up++)
            if (spawn_cell(cx + 1, cy - up + 1, id)) { made++; break; }
    }
    return made;
}

/* ------------------------------------------------------------- trapdoor */
void sand_door_open(float xc, float r_target, float rate, int hold_min, float close_fill)
{
    if (door.state == SAND_DOOR_CLOSED || door.state == SAND_DOOR_CLOSING) {
        if (door.state == SAND_DOOR_CLOSED) door.r = 0;
        door.xc = xc;
        door.state = SAND_DOOR_OPENING;
    }
    door.r_target = r_target; door.rate = rate; door.hold = 0; door.hold_min = hold_min; door.close_fill = close_fill;
}
void  sand_door_extend(void) { if (door.state == SAND_DOOR_OPEN) door.hold = 0; }
int   sand_door_state(void) { return door.state; }
float sand_door_radius(void) { return door.r; }
float sand_door_center(void) { return door.xc; }

static void door_tick(void)
{
    if (door.state == SAND_DOOR_CLOSED) return;
    switch (door.state) {
    case SAND_DOOR_OPENING:
        door.r += door.rate;
        if (door.r >= door.r_target) { door.r = door.r_target; door.state = SAND_DOOR_OPEN; door.hold = 0; }
        break;
    case SAND_DOOR_OPEN:
        door.hold++;
        if ((door.hold >= door.hold_min && sand_fill() <= door.close_fill) || door.hold >= 480) door.state = SAND_DOOR_CLOSING;
        break;
    case SAND_DOOR_CLOSING:
        door.r -= door.rate > 1.0f ? door.rate : 1.0f;
        if (door.r < 0) door.r = 0;
        break;
    default: break;
    }
    int missing = 0;
    for (int x = 1; x <= SD_W; x++) {
        int open = fabsf((float)(x - 1) + 0.5f - door.xc) < door.r;
        for (int y = PLANK0; y <= ROW_LAST; y++) {
            int i = (y << 8) + x;
            if (open) { if (g[i] == ID_PLANK) { g[i] = 0; v[i] = 0; mark(x, y); } }
            else if (g[i] == 0) { g[i] = ID_PLANK; mark(x, y); }
            else if (g[i] != ID_PLANK) missing++;              /* a grain is still passing through */
        }
    }
    if (door.state == SAND_DOOR_CLOSING && door.r <= 0 && missing == 0) door.state = SAND_DOOR_CLOSED;
}

static void drain_sink(void)
{
    drained_n = 0;
    for (int y = SINK0; y <= SINK1; y++)
        for (int x = 1; x <= SD_W; x++) {
            int i = (y << 8) + x;
            uint8_t c = g[i];
            if (!c) continue;
            if (c < ID_STATIC) {
                n_sand--;
                if (drained_n < 16 && (x & 7) == 0) { drained_h[drained_n] = (uint8_t)((c - 1) >> 2); drained_x[drained_n++] = (uint8_t)(x - 1); }
            }
            g[i] = 0; v[i] = 0;
        }
}

int sand_drained(int *hues, int *cols, int max)
{
    int n = drained_n < max ? drained_n : max;
    for (int i = 0; i < n; i++) { hues[i] = drained_h[i]; cols[i] = drained_x[i]; }
    return n;
}

/* ------------------------------------------------------------- the rules */
static long visited;

static void row_segment(int y, int x0, int x1, int dir)
{
    uint8_t *G = g, *V = v;
    const int step = dir ? -1 : 1;
    int x = dir ? x1 : x0;
    const int end = dir ? x0 - 1 : x1 + 1;
    for (; x != end; x += step) {
        const int i = (y << 8) + x;
        const uint8_t c = G[i];
        if (c == 0 || c >= ID_STATIC) continue;
        visited++;
        /* a grain takes up to `sp` steps per tick (sp grows 1,2,3 while it keeps moving), every step goes down a row:
           straight down if free, else diagonally if the side cell and the diagonal cell are both free */
        int sp = V[i] < FALL_MAX ? V[i] + 1 : FALL_MAX;
        int cur = i, moved = 0;
        const uint32_t r = rnd();
        const int d = (r & 1) ? 1 : -1;
        for (int n = 0; n < sp; n++) {
            if (G[cur + PW] == 0) cur += PW;
            else if (!G[cur + d] && !G[cur + PW + d]) cur += PW + d;
            else if (!G[cur - d] && !G[cur + PW - d]) cur += PW - d;
            else if (n == 0 && ((r >> 1) & 3) == 0 && !G[cur + d] && G[cur + PW + d] && !G[cur + 2 * d] && !G[cur + PW + 2 * d]) cur += PW + 2 * d;
            else break;
            moved++;
        }
        if (moved) {
            G[cur] = c; V[cur] = (uint8_t)sp; G[i] = 0; V[i] = 0;
            mark(x, y); mark(cur & 255, cur >> 8);
        } else V[i] = 0;
    }
}

void sand_step(void)
{
    double t0 = now_ms();
    tick++;
    visited = 0;
    door_tick();
    const int dir = (int)(tick & 1u);
    /* a slow round-robin wake so nothing can ever hang in mid-air forever */
    { int c = (int)(tick % (uint32_t)(CW * CHN)); if (!act[c]) act[c] = 1; }
    for (int cy = CHN - 1; cy >= 0; cy--) {
        const uint8_t *arow = &act[cy * CW];
        unsigned any = 0;
        for (int k = 0; k < CW; k++) any |= arow[k];
        if (!any) continue;
        int ylo = cy * CHK, yhi = ylo + CHK - 1;
        if (yhi > SINK1) yhi = SINK1;
        if (ylo < ROW_FIRST) ylo = ROW_FIRST;
        for (int y = yhi; y >= ylo; y--)
            for (int k = 0; k < CW; k++) {
                const int cx = dir ? CW - 1 - k : k;
                if (!arow[cx]) continue;
                int x0 = cx * CHK, x1 = x0 + CHK - 1;
                if (x0 < 1) x0 = 1;
                if (x1 > SD_W) x1 = SD_W;
                if (x0 <= x1) row_segment(y, x0, x1, dir);
            }
    }
    if (door.state != SAND_DOOR_CLOSED) drain_sink(); else drained_n = 0;
    for (int c = 0; c < CW * CHN; c++) if (act[c]) act[c]--;
    double dt = now_ms() - t0;
    st_sim_ms += dt; if (dt > st_sim_max) st_sim_max = dt; st_ticks++; st_cells += visited;
}

/* ----------------------------------------------------------------- status */
int   sand_count(void) { return n_sand; }
float sand_fill(void) { return capacity > 0 ? (float)n_sand / (float)capacity : 0.0f; }
int   sand_ceiling(void) { return ceil_count; }

void sand_scan(int heights[SD_GROUPS], int *peak_x)
{
    const int gw = SD_W / SD_GROUPS;
    int best = -1, bx = SD_W / 2;
    for (int gi = 0; gi < SD_GROUPS; gi++) {
        int h = 0;
        for (int k = 0; k < 4; k++) {
            const int x = gi * gw + 2 + k * (gw / 4);
            int n = 0;
            for (int y = PLANK0 - 1; y >= ROW_FIRST; y--) {      /* contiguous settled sand rising from the floor */
                const int i = (y << 8) + x + 1;
                if (!g[i] || g[i] >= ID_STATIC || v[i]) break;
                n++;
            }
            if (n > h) h = n;
            if (n > best) { best = n; bx = x; }
        }
        heights[gi] = h;
    }
    if (peak_x) *peak_x = bx;
    int cnt = 0;
    for (int y = ROW_FIRST; y < ROW_FIRST + 5; y++)
        for (int x = 1; x <= SD_W; x++) { const int i = (y << 8) + x; if (g[i] && g[i] < ID_STATIC && !v[i]) cnt++; }
    ceil_count = cnt;
}

/* --------------------------------------------------------------- drawing */
static void relight_chunk(int cx, int cy)
{
    int x0 = cx * CHK, x1 = x0 + CHK - 1, y0 = cy * CHK, y1 = y0 + CHK - 1;
    if (x0 < 1) x0 = 1;
    if (x1 > SD_W) x1 = SD_W;
    if (y0 < 1) y0 = 1;
    if (y1 > SD_H) y1 = SD_H;
    for (int y = y0; y <= y1; y++) {
        uint32_t *out = &px[(y - 1) * SD_W];
        const uint32_t *sp = &spx[(y - 1) * SD_W];
        for (int x = x0; x <= x1; x++) {
            const int i = (y << 8) + x;
            const uint8_t c = g[i];
            uint32_t p;
            if (c == 0) p = g[i - PW - 1] ? SHADOW_PX : 0;
            else if (c < ID_STATIC) p = sand_lut[c][1 + (g[i - PW - 1] == 0) - (g[i + PW + 1] == 0)];
            else p = sp[x - 1];
            out[x - 1] = p;
        }
    }
}

void sand_draw(SDL_Renderer *r, int dx, int dy)
{
    double t0 = now_ms();
    int miny = SD_H, maxy = -1;
    for (int cy = 0; cy < CHN; cy++)
        for (int cx = 0; cx < CW; cx++) {
            if (!dirty[cy * CW + cx]) continue;
            dirty[cy * CW + cx] = 0;
            if (cy * CHK + 1 > SD_H) continue;
            relight_chunk(cx, cy);
            int a = cy * CHK - 1, b = cy * CHK + CHK - 2;          /* texture rows covered by this chunk */
            if (a < 0) a = 0;
            if (b > SD_H - 1) b = SD_H - 1;
            if (a < miny) miny = a;
            if (b > maxy) maxy = b;
        }
    if (maxy >= miny) {
        SDL_Rect rc = { 0, miny, SD_W, maxy - miny + 1 };
        SDL_UpdateTexture(tex, &rc, &px[miny * SD_W], SD_W * 4);
    }
    st_light_ms += now_ms() - t0; st_frames++;
    SDL_FRect d = { (float)dx, (float)dy, (float)SD_PX_W, (float)SD_PX_H };
    SDL_RenderCopyF(r, tex, NULL, &d);
}
