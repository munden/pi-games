/* SAND POUR - rainbow sand falls from your fingers into a toy window frame on a sunny beach.
 *
 * The sand is a cellular automaton (sand.c) drawn as a streaming texture between two 3D layers:
 *   layer 1  sky, sea, sun with a face, clouds, beach floor, palm tree, the back wall and inner walls of the box
 *   (sand)   the lit sand texture, flat on the z=0 plane, nearest-filtered, 3x3 canvas px per grain
 *   layer 2  the wooden frame with toy-block corners, the big trapdoor button, flowers, ball, bucket, glows, sparkles
 * Touch: every finger pours sand in its own colour; touches on the decor make it giggle; the big red button opens
 * the trapdoor.  Idle: a funnel trickles rainbow sand, a ghost hand shows how to pour.  Nothing can go wrong.
 */
#include "pg3d.h"
#include "art.h"
#include "sand.h"

#include <math.h>
#include <string.h>

#define MAX_FX    160
#define DIST      (270.0f / tanf(CAM_FOV * 0.5f * PG_PI / 180.0f))

enum { P_SUN, P_PALM, P_BALL, P_BUCKET, P_F0, P_F1, P_F2, P_COUNT };
typedef struct { float sx, sy, r, t; } Poke;                         /* a ticklish toy: screen hit circle + wobble 1..0 */
typedef struct { bool on, btn, has_last; float hue, lx, ly; } Finger;
typedef struct { PG_V3 p, v; float life, max, hue, size, rot; uint8_t kind; } Fx;   /* kind 0 star, 1 puff */

static Art    A;                                                      /* every texture and mesh */
static Poke   pokes[P_COUNT];
static Finger fing[PG_MAX_TOUCH];
static Fx     fx[MAX_FX];

static float sway_x, sway_y;
static int   win_rx, win_ry;
static float btn_sx, btn_sy, btn_r = 60.0f;
static float btn_press, btn_bounce;
static int   idle_ticks, door_prev, cooldown_auto, ceil_ticks, scan_n, shh_t;
static float hint_a, ghost_t, ghost_hue, funnel_hue, hue_counter, ghost_px, ghost_py;
static int   rec_band[SD_GROUPS];
static int   chime_last = -100, peak_x = SD_W / 2, peak_h;
static float perf_scale = 1.0f;

/* ------------------------------------------------------------------- helpers */
static float kdepth(float z) { return DIST / (DIST - z); }
static PG_V3 P3(float sx, float sy, float z) { float k = kdepth(z); return pg_v3((sx - 480.0f) / k, (270.0f - sy) / k, z); }
static float WS(float px, float z) { return px / kdepth(z); }
static int   hue_index(float hue) { int h = (int)(hue * ((float)SD_HUES / 360.0f)); return ((h % SD_HUES) + SD_HUES) % SD_HUES; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static void fx_add(uint8_t kind, PG_V3 p, PG_V3 v, float life, float hue, float size)
{
    for (int i = 0; i < MAX_FX; i++) {
        if (fx[i].life > 0) continue;
        fx[i] = (Fx){ p, v, life, life, hue, size, pg_rand_range(0, PG_TAU), kind };
        return;
    }
}

static void fx_burst(PG_V3 p, float hue, int n, float speed)
{
    for (int i = 0; i < n; i++) {
        float a = pg_rand_range(0, PG_TAU), s = pg_rand_range(0.4f, 1.0f) * speed;
        fx_add(0, p, pg_v3(cosf(a) * s, sinf(a) * s + 60.0f, pg_rand_range(-20, 20)), pg_rand_range(0.7f, 1.2f), hue + pg_rand_range(-40, 40), pg_rand_range(14, 24));
    }
}

static bool in_win(float x, float y) { return x >= (float)win_rx && x < (float)(win_rx + SD_PX_W) && y >= (float)win_ry && y < (float)(win_ry + SD_PX_H); }
static bool in_gate(float x, float y) { return y < 100.0f && (x < 100.0f || x > (float)PG_W - 100.0f); }   /* the parent's exit corners: never used */
static bool hit_button(float x, float y) { float dx = x - btn_sx, dy = y - btn_sy, r = btn_r + 14.0f; return dx * dx + dy * dy <= r * r; }

/* nobody has touched the screen for 15 s: the show goes on but silently (a toy left on must not chime all night) */
static bool attended(void) { return idle_ticks < 900; }

static bool chime(int step, float vol)             /* at most one every 16 ticks, so piles growing everywhere stay calm */
{
    if (!attended() || (int)pg_ticks() - chime_last < 16) return false;
    chime_last = (int)pg_ticks();
    pg_sfx_chime(step, vol);
    return true;
}

/* ---------------------------------------------------------------------- init */
static int init(SDL_Renderer *r)
{
    art_init(&A);
    sand_init(r, pg_rand());
    pg3d_sky(pg_hex(0x5DB8F2), pg_hex(0xE6F6F4));
    pg3d_light(pg_v3(-0.43f, 0.72f, 0.53f), pg_hex(0xB8B0A0), pg_hex(0x707C98), pg_hex(0x7A6C5C));
    pg3d_fog(pg_hex(0xD3F1F0), 820.0f, 1320.0f);
    for (int i = 0; i < SD_GROUPS; i++) rec_band[i] = 0;
    door_prev = SAND_DOOR_CLOSED;
    ghost_hue = pg_rand_range(0, 360); funnel_hue = pg_rand_range(0, 360); hue_counter = pg_rand_range(0, 360);
    for (int i = 0; i < P_COUNT; i++) { pokes[i].sx = -1000; pokes[i].sy = -1000; pokes[i].r = 40; }
    return 0;
}

static void shutdown(void)
{
    sand_shutdown();
    art_shutdown(&A);
}

/* -------------------------------------------------------------------- update */
static void press_button(void)
{
    int st = sand_door_state();
    if (st == SAND_DOOR_CLOSED || st == SAND_DOOR_CLOSING) pg_sfx_whoosh(0.6f);
    sand_door_open((float)SD_W * 0.5f, (float)SD_W * 0.5f + 4.0f, 3.2f, 72, 0.012f);
    sand_door_extend();
    btn_bounce = 1.0f;
    PG_V3 p;
    if (pg3d_plane_z(btn_sx, btn_sy, 70, &p)) fx_burst(p, 5.0f, 8, 150.0f);
}

static void poke_toy(int k, float x, float y)
{
    pokes[k].t = 1.0f;
    PG_V3 p;
    if (pg3d_plane_z(x, y, 70, &p)) fx_burst(p, (float)(k * 47), 10, 190.0f);
    switch (k) {
    case P_SUN:    pg_sfx_sparkle(0.45f); break;
    case P_PALM:   pg_sfx_chime(5, 0.45f); break;
    case P_BALL:   pg_sfx_boing(0.5f, 1.1f); break;
    case P_BUCKET: pg_sfx_tap(0.6f); pg_sfx_chime(1, 0.3f); break;
    default:       pg_sfx_chime(4 + k - P_F0, 0.5f); break;
    }
}

static void touches(void)
{
    const PG_Touch *t = pg_touches();
    int pouring = 0;
    const int n = (int)(6.0f * perf_scale + 0.5f);
    for (int i = 0; i < PG_MAX_TOUCH; i++) {
        Finger *f = &fing[i];
        if (t[i].state == PG_TOUCH_NONE) { f->on = f->btn = f->has_last = false; continue; }
        idle_ticks = 0;
        if (t[i].state == PG_TOUCH_UP) { f->on = f->btn = f->has_last = false; continue; }
        const float x = t[i].x, y = t[i].y;
        if (t[i].state == PG_TOUCH_DOWN) {
            f->has_last = false;
            f->btn = hit_button(x, y);
            f->on = !f->btn;
            f->hue = fmodf(hue_counter, 360.0f);
            hue_counter = fmodf(hue_counter + 137.5f + pg_rand_range(-12, 12), 360.0f);
            if (f->btn) press_button();
            else {
                PG_V3 p;
                if (pg3d_plane_z(x, y, 70, &p)) fx_burst(p, f->hue, 5, 130.0f);
                if (in_win(x, y) && !in_gate(x, y)) pg_sfx_chime(hue_index(f->hue) % 5 + 3, 0.35f);
                else if (in_gate(x, y)) pg_sfx_chime(2, 0.25f);
                else {
                    int hit = -1; float best = 1e9f;
                    for (int k = 0; k < P_COUNT; k++) {
                        float dx = x - pokes[k].sx, dy = y - pokes[k].sy, d = sqrtf(dx * dx + dy * dy);
                        if (d < pokes[k].r + 14.0f && d < best) { best = d; hit = k; }
                    }
                    if (hit >= 0) poke_toy(hit, x, y);
                    else pg_sfx_chime((int)(x * 10.0f / PG_W), 0.4f);
                }
            }
        }
        if (f->btn || !f->on) continue;
        if (in_win(x, y) && !in_gate(x, y)) {
            pouring++;
            const float cx = (x - (float)win_rx) / (float)SD_CELL, cy = (y + 10.0f - (float)win_ry) / (float)SD_CELL;
            if (!f->has_last) { f->lx = cx; f->ly = cy; f->has_last = true; }
            float seg = fabsf(cx - f->lx) + fabsf(cy - f->ly);
            sand_pour(f->lx, f->ly, cx, cy, hue_index(f->hue), n + (int)clampf(seg * 0.5f, 0, 6));
            if ((t[i].ticks % 5) == 2) {                          /* a soft puff of dust where the stream lands */
                int row = sand_land_row((int)cx, (int)cy);
                if (row >= 0) fx_add(1, pg_v3(WX0 + (floorf(cx) + 0.5f) * (float)SD_CELL, WYT - ((float)row + 0.5f) * (float)SD_CELL + 3.0f, 40.0f),
                                     pg_v3(pg_rand_range(-10, 10), pg_rand_range(8, 24), 0), 0.5f, f->hue, 20.0f);
            }
            f->lx = cx; f->ly = cy;
            f->hue = fmodf(f->hue + 0.5f, 360.0f);
        } else {
            f->has_last = false;
            if (t[i].state == PG_TOUCH_HELD && (t[i].ticks % 9) == 0) {
                PG_V3 p;
                if (pg3d_plane_z(x, y, 70, &p)) fx_burst(p, f->hue, 2, 90.0f);
            }
        }
    }
    if (pouring && (++shh_t % 4) == 0)
        pg_tone(PG_WAVE_NOISE, 1900.0f, 950.0f, 260.0f, 0.065f + 0.012f * (float)(pouring > 5 ? 5 : pouring));
    if (!pouring) shh_t = 0;
}

static void idle_show(void)
{
    /* the funnel at the top of the box trickles rainbow sand by itself */
    funnel_hue = fmodf(funnel_hue + 0.8f, 360.0f);
    if (sand_fill() < 0.9f) sand_pour((float)SD_W * 0.5f - 9.0f, 6.0f, (float)SD_W * 0.5f + 9.0f, 9.0f, hue_index(funnel_hue), (pg_ticks() % 3 == 0) ? 1 : 2);

    /* after a short while without touches a ghost hand shows how to pour */
    idle_ticks++;
    float target = idle_ticks > 130 ? 1.0f : 0.0f;
    hint_a += (target - hint_a) * (target > hint_a ? 0.05f : 0.2f);
    if (hint_a > 0.02f) {
        ghost_t = fmodf(ghost_t + PG_DT, 1000.0f);
        const float gx = (float)SD_W * 0.5f + 84.0f * sinf(ghost_t * 0.7f), gy = 20.0f + 5.0f * sinf(ghost_t * 1.6f);
        ghost_px = (float)win_rx + gx * (float)SD_CELL; ghost_py = (float)win_ry + gy * (float)SD_CELL - 12.0f;
        if (hint_a > 0.6f) {
            ghost_hue = fmodf(ghost_hue + 0.6f, 360.0f);
            sand_pour(gx - 1.0f, gy, gx, gy, hue_index(ghost_hue), 3);
        }
    }
}

static void door_logic(void)
{
    const int st = sand_door_state();
    if (st == SAND_DOOR_CLOSED) {
        if (door_prev != SAND_DOOR_CLOSED && attended()) pg_sfx_thud(0.3f);
        if (cooldown_auto > 0) cooldown_auto--;
        else {
            float fill = sand_fill();
            /* the box is nearly full, or a pile has grown up to the ceiling / the funnel: let it sink slowly through a small hole */
            if (fill > 0.84f || ceil_ticks > 100 || peak_h >= 74) {
                float close_at = fill > 0.84f ? 0.55f : fmaxf(0.04f, fill - 0.12f);
                sand_door_open((float)peak_x + 0.5f, fill > 0.92f ? 12.0f : 3.0f, 0.12f, 150, close_at);
                if (attended()) pg_sfx_whoosh(0.22f);
                cooldown_auto = 600;
            }
        }
    }
    door_prev = st;
    float want = (st == SAND_DOOR_OPENING || st == SAND_DOOR_OPEN) && sand_door_radius() > 8.0f ? 1.0f : 0.0f;
    btn_press += (want - btn_press) * 0.2f;
    btn_bounce *= 0.86f;
}

static void scan_piles(void)
{
    int h[SD_GROUPS], hx[SD_GROUPS];
    sand_scan(h, hx, &peak_x);
    peak_h = 0;
    for (int g = 0; g < SD_GROUPS; g++) if (h[g] > peak_h) peak_h = h[g];
    ceil_ticks = sand_ceiling() > 14 ? ceil_ticks + 6 : 0;
    for (int g = 0; g < SD_GROUPS; g++) {
        int band = h[g] / 10;
        if (band > rec_band[g]) {
            if (chime(band - 1 > 9 ? 9 : band - 1, 0.55f))   /* ...and a little shower of stars at the top of the pile that just grew */
                fx_burst(pg_v3(WX0 + ((float)hx[g] + 0.5f) * (float)SD_CELL, WYT - ((float)(SD_H - SD_PLANK - 1 - h[g]) + 0.5f) * (float)SD_CELL + 6.0f, 62.0f),
                         (float)band * 38.0f, 6, 120.0f);
            rec_band[g] = band;
        } else if (band < rec_band[g] - 1) rec_band[g] = band;
    }
}

static void fx_update(void)
{
    for (int i = 0; i < MAX_FX; i++) {
        Fx *f = &fx[i];
        if (f->life <= 0) continue;
        f->life -= PG_DT;
        if (f->kind == 0) f->v.y -= 300.0f * PG_DT;
        f->p = pg_v3_add(f->p, pg_v3_scale(f->v, PG_DT));
        /* confetti that falls onto the floor of the box fades out there instead of dropping through the frame */
        if (f->kind == 0 && f->p.y < WYB + 6.0f && f->p.y > WYB - 40.0f && f->p.x > WX0 && f->p.x < WX1 && f->life > 0.12f) f->life = 0.12f;
    }
    for (int i = 0; i < P_COUNT; i++) pokes[i].t = fmaxf(0.0f, pokes[i].t - PG_DT * 1.5f);
}

static void update(void)
{
    const float tm = pg_time();
    sway_x = 14.0f * sinf(tm * 0.27f) + 5.0f * sinf(tm * 0.71f + 1.3f);
    sway_y = 5.0f * sinf(tm * 0.19f + 0.6f);
    win_rx = (int)floorf(WIN_X - sway_x + 0.5f);
    win_ry = (int)floorf(WIN_Y + sway_y + 0.5f);
    /* ease off if the device is struggling and the sand is a real part of the cost (headless software rendering is not) */
    perf_scale = (pg_frame_ms() > 12.0f && sand_cpu_ms() > 2.0f) ? 0.5f : 1.0f;

    touches();
    idle_show();
    door_logic();
    sand_step();
    if ((++scan_n % 6) == 0) scan_piles();

    /* sand leaving through the trapdoor puffs out of the slot */
    int hue[16], col[16];
    int nd = sand_drained(hue, col, 16);
    for (int i = 0; i < nd; i++)
        if (pg_rand() % 3 == 0)
            fx_add(1, pg_v3(WX0 + ((float)col[i] + 0.5f) * (float)SD_CELL, WYB + 6.0f, ZF * 0.55f), pg_v3(pg_rand_range(-14, 14), pg_rand_range(20, 60), 0),
                   pg_rand_range(0.5f, 0.9f), (float)hue[i] * (360.0f / (float)SD_HUES), pg_rand_range(22, 36));
    fx_update();
}

/* -------------------------------------------------------------------- render */
static const PG_Color WHITE = { 255, 255, 255, 255 };

static void draw_sky_props(float tm)
{
    /* sun with a happy face: soft glow, slowly turning rays, the face */
    const float z = -520.0f;
    PG_V3 sp = P3(88, 126, z);
    float pk = pokes[P_SUN].t, pulse = 1.0f + 0.16f * pk * sinf(pk * 12.0f) + 0.025f * sinf(tm * 1.6f);
    int mode = pk > 0.1f ? 2 : (fmodf(tm, 4.3f) > 4.15f ? 1 : 0);
    pg3d_sprite(pg3d_builtin(PG3D_TEX_GLOW), sp, WS(330, z) * pulse, WS(330, z) * pulse, 0, pg_rgba(255, 232, 150, 150), PG3D_BG | PG3D_NOFOG);
    pg3d_sprite(A.rays, sp, WS(168, z) * pulse, WS(168, z) * pulse, tm * 0.12f + pk * 2.0f, WHITE, PG3D_BG | PG3D_NOFOG);
    pg3d_sprite(A.sun[mode], sp, WS(108, z) * pulse, WS(108, z) * pulse, 0.04f * sinf(tm * 0.8f), WHITE, PG3D_BG | PG3D_NOFOG);
    float pxs, pys, dd;
    if (pg3d_project(sp, &pxs, &pys, &dd)) { pokes[P_SUN].sx = pxs; pokes[P_SUN].sy = pys; pokes[P_SUN].r = 56; }

    pg3d_sprite(A.cloud, P3(150.0f + 16.0f * sinf(tm * 0.2f), 58, -480), WS(104, -480), WS(52, -480), 0, WHITE, PG3D_BG | PG3D_NOFOG);
    pg3d_sprite(A.cloud, P3(44.0f + 12.0f * sinf(tm * 0.17f + 1.0f), 214, -470), WS(84, -470), WS(42, -470), 0, pg_rgba(255, 255, 255, 235), PG3D_BG | PG3D_NOFOG);
}

static void draw_palm(float tm)
{
    const float pk = pokes[P_PALM].t;
    PG_V3 base = P3(96, 408, -60.0f);
    const float shake = pk * sinf(tm * 15.0f);
    PG_M4 m;
    float px0 = 0, py0 = 0;
    for (int i = 0; i < 6; i++) {
        float t1 = (float)(i + 1) / 6.0f;
        float x1 = 36.0f * t1 * t1 + (0.05f * sinf(tm * 1.1f) + 0.06f * shake) * 150.0f * t1 * t1, y1 = 196.0f * t1;
        float len = sqrtf((x1 - px0) * (x1 - px0) + (y1 - py0) * (y1 - py0)), ang = -atan2f(x1 - px0, y1 - py0);
        float rad = 1.3f - 0.55f * (t1 - 0.08f);
        m = pg_m4_mul(pg_m4_translate(base.x + (px0 + x1) * 0.5f, base.y + (py0 + y1) * 0.5f, base.z), pg_m4_mul(pg_m4_rot_z(ang), pg_m4_scale(rad, len / 40.0f * 1.15f, rad)));
        pg3d_mesh(A.trunk_seg, A.trunk, &m, WHITE, PG3D_BG);
        px0 = x1; py0 = y1;
    }
    PG_V3 c = pg_v3(base.x + px0, base.y + py0, base.z);
    static const float ang[4] = { 0.62f, 0.12f, -0.34f, 1.18f }, len[4] = { 1.0f, 1.08f, 0.92f, 0.74f }, ph[4] = { 0.0f, 1.3f, 2.1f, 3.0f };
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < 4; i++) {
            float a = ang[i] + 0.05f * sinf(tm * 1.2f + ph[i] + (float)pass) + 0.16f * shake * sinf((float)i + 1.0f);
            float s = len[i] * 1.0f;
            PG_M4 R = pass ? pg_m4_mul(pg_m4_rot_z(-a), pg_m4_scale(-s, s, 1)) : pg_m4_mul(pg_m4_rot_z(a), pg_m4_scale(s, s, 1));
            m = pg_m4_mul(pg_m4_translate(c.x, c.y, c.z + (float)(i & 1) * 3.0f), R);
            PG_Color tint = (i & 1) ? pg_rgb(255, 255, 255) : pg_rgb(214, 238, 214);
            pg3d_mesh(A.leaf_m, A.leaf, &m, tint, PG3D_BG | PG3D_TWOSIDED | PG3D_UNLIT);
        }
    for (int i = 0; i < 3; i++) {
        m = pg_m4_translate(c.x + (float)(i - 1) * 9.0f, c.y - 10.0f - (float)(i & 1) * 4.0f + 2.0f * shake, c.z + 6.0f);
        pg3d_mesh(A.coconut_m, A.trunk, &m, pg_rgb(210, 170, 130), PG3D_BG);
    }
    float sx, sy, d;
    if (pg3d_project(c, &sx, &sy, &d)) { pokes[P_PALM].sx = sx; pokes[P_PALM].sy = sy; pokes[P_PALM].r = 74; }
}

static void shadows(void)
{
    const float gy = GROUND_Y + 0.6f;
    for (int i = 0; i < 6; i++) pg3d_blob_shadow(WX0 - 40.0f + (float)i * 148.0f, gy, 4.0f, 118.0f, 0.34f);
    pg3d_blob_shadow(-322.0f, gy, 48.0f, 112.0f, 0.5f);          /* crate */
    pg3d_blob_shadow(P3(96, 420, -60).x + 26.0f, gy, -60.0f, 60.0f, 0.4f);   /* palm */
}

static void draw_back(float tm)
{
    PG_M4 m;
    m = pg_m4_translate(0, 8.0f - 200.0f, -700.0f);
    pg3d_mesh(A.sea_quad, A.sea, &m, WHITE, PG3D_BG | PG3D_UNLIT | PG3D_NOFOG);
    draw_sky_props(tm);
    for (int ix = 0; ix < 4; ix++)
        for (int iz = 0; iz < 2; iz++) {
            m = pg_m4_translate(-750.0f + 500.0f * (float)ix, GROUND_Y, -150.0f + 500.0f * (float)iz);
            pg3d_mesh(A.ground_tile, A.ground, &m, WHITE, PG3D_BG);
        }
    shadows();
    draw_palm(tm);

    /* the box behind the sand: pastel back wall and the dark inner walls (seen in perspective as the camera sways) */
    m = pg_m4_translate((WX0 + WX1) * 0.5f, (WYT + WYB) * 0.5f, -ZB);
    pg3d_mesh(A.panel, A.wall, &m, WHITE, PG3D_BG | PG3D_UNLIT | PG3D_NOFOG);
    m = pg_m4_identity();
    pg3d_mesh(A.tun_l, A.wood_v, &m, pg_rgb(176, 150, 130), PG3D_BG | PG3D_TWOSIDED | PG3D_NOFOG);
    pg3d_mesh(A.tun_r, A.wood_v, &m, pg_rgb(150, 126, 110), PG3D_BG | PG3D_TWOSIDED | PG3D_NOFOG);
    pg3d_mesh(A.tun_t, A.wood_h, &m, pg_rgb(150, 126, 110), PG3D_BG | PG3D_TWOSIDED | PG3D_NOFOG);
    pg3d_mesh(A.tun_b, A.wood_h, &m, pg_rgb(200, 172, 150), PG3D_BG | PG3D_TWOSIDED | PG3D_NOFOG);
}

static void draw_frame(void)
{
    const float cx = (WX0 + WX1) * 0.5f, cy = (WYT + WYB) * 0.5f, zc = ZF * 0.5f;
    PG_M4 m;
    m = pg_m4_translate(cx, WYT + BAR_T * 0.5f, zc);          pg3d_mesh(A.bar_top, A.wood_h, &m, WHITE, 0);
    m = pg_m4_translate(cx, WYB - BAR_B * 0.5f, zc);          pg3d_mesh(A.bar_bot, A.wood_h, &m, WHITE, 0);
    m = pg_m4_translate(WX0 - BAR_T * 0.5f, cy, zc);          pg3d_mesh(A.bar_l, A.wood_v, &m, WHITE, 0);
    m = pg_m4_translate(WX1 + BAR_T * 0.5f, cy, zc);          pg3d_mesh(A.bar_r, A.wood_v, &m, WHITE, 0);
    m = pg_m4_translate(WX0 - BAR_T * 0.5f, WYT + BAR_T * 0.5f, zc);   pg3d_mesh(A.post_t, A.post, &m, pg_hex(0xF2544E), 0);
    m = pg_m4_translate(WX1 + BAR_T * 0.5f, WYT + BAR_T * 0.5f, zc);   pg3d_mesh(A.post_t, A.post, &m, pg_hex(0xFFD23A), 0);
    m = pg_m4_translate(WX0 - BAR_T * 0.5f, WYB - BAR_B * 0.5f, zc);   pg3d_mesh(A.post_b, A.post, &m, pg_hex(0x3C8DF0), 0);
    m = pg_m4_translate(WX1 + BAR_T * 0.5f, WYB - BAR_B * 0.5f, zc);   pg3d_mesh(A.post_b, A.post, &m, pg_hex(0x4CC95A), 0);

    m = pg_m4_translate(cx, WYB - BAR_B * 0.5f - 1.0f, ZF + 0.4f);
    pg3d_mesh(A.trim, pg3d_white(), &m, WHITE, PG3D_UNLIT);

    /* the slot in the base where the sand disappears: as wide as the open trapdoor */
    const float r = sand_door_radius();
    if (r > 0.3f) {
        float w = fminf(r, (float)SD_W * 0.5f) * (float)SD_CELL;
        float xs = WX0 + sand_door_center() * (float)SD_CELL;
        float x0 = fmaxf(WX0, xs - w), x1 = fminf(WX1, xs + w);
        m = pg_m4_mul(pg_m4_translate((x0 + x1) * 0.5f, WYB + 0.8f, 27.0f), pg_m4_mul(pg_m4_rot_x(-PG_PI * 0.5f), pg_m4_scale((x1 - x0) * 0.5f, 23.0f, 1.0f)));
        pg3d_mesh(A.slot_m, A.post, &m, pg_hex(0x4A2E58), PG3D_UNLIT | PG3D_TWOSIDED);
    }
}

static void draw_crate_and_button(float tm)
{
    const float face_z = 90.0f, cyb = GROUND_Y + 58.0f, cxw = -326.0f;
    PG_M4 m = pg_m4_translate(cxw, cyb, face_z - 42.0f);
    pg3d_mesh(A.crate_m, A.crate, &m, WHITE, 0);
    const float press = btn_press, bump = btn_bounce;
    m = pg_m4_mul(pg_m4_translate(cxw, cyb, face_z + 5.0f), pg_m4_rot_x(PG_PI * 0.5f));
    pg3d_mesh(A.btn_ring, A.btnbase, &m, WHITE, 0);
    const float zcap = face_z + 10.0f + 9.0f - 7.0f * press + 3.0f * bump;
    m = pg_m4_mul(pg_m4_translate(cxw, cyb, zcap), pg_m4_rot_x(PG_PI * 0.5f));
    pg3d_mesh(A.btn_cap, A.btn, &m, WHITE, 0);
    PG_V3 gp = pg_v3(cxw, cyb, zcap + 24.0f);
    float glow = 0.20f + 0.10f * sinf(tm * 3.0f) + 0.4f * bump;
    pg3d_sprite(pg3d_builtin(PG3D_TEX_GLOW), gp, 180, 180, 0, pg_rgba(255, 110, 90, (uint8_t)(clampf(glow, 0, 1) * 255.0f)), PG3D_ADDITIVE);
    float sx, sy, d, sx2, sy2;
    if (pg3d_project(pg_v3(cxw, cyb, zcap + 9.0f), &sx, &sy, &d) && pg3d_project(pg_v3(cxw + 47.0f, cyb, zcap + 9.0f), &sx2, &sy2, &d)) {
        btn_sx = sx; btn_sy = sy; btn_r = sx2 - sx;
    }
}

static void draw_decor(float tm)
{
    PG_M4 m;
    /* beach ball */
    {
        PG_V3 p = P3(318, 506, 130.0f);
        float sq = pokes[P_BALL].t * 0.28f * sinf(pokes[P_BALL].t * 14.0f), hop = pokes[P_BALL].t * 26.0f * fabsf(sinf(pokes[P_BALL].t * 7.0f));
        m = pg_m4_mul(pg_m4_translate(p.x, GROUND_Y + 30.0f * (1 - sq) + hop, p.z), pg_m4_mul(pg_m4_rot_y(tm * 0.25f + pokes[P_BALL].t * 5.0f), pg_m4_scale(1 + sq, 1 - sq, 1 + sq)));
        pg3d_mesh(A.ball_m, A.ball, &m, WHITE, 0);
        pg3d_shine(pg_v3(p.x, GROUND_Y + 30.0f + hop, p.z), 30.0f, 0.8f);
        pg3d_blob_shadow(p.x, GROUND_Y + 0.7f, p.z, 40.0f - hop * 0.3f, 0.45f);
        float sx, sy, d;
        if (pg3d_project(pg_v3(p.x, GROUND_Y + 30.0f, p.z), &sx, &sy, &d)) { pokes[P_BALL].sx = sx; pokes[P_BALL].sy = sy; pokes[P_BALL].r = 40; }
    }
    /* bucket and spade */
    {
        PG_V3 p = P3(812, 510, 140.0f);
        float wob = pokes[P_BUCKET].t * 0.35f * sinf(pokes[P_BUCKET].t * 16.0f);
        m = pg_m4_mul(pg_m4_translate(p.x, GROUND_Y + 24.0f, p.z), pg_m4_rot_z(wob));
        pg3d_mesh(A.bucket_m, A.bucket, &m, WHITE, 0);
        m = pg_m4_mul(pg_m4_translate(p.x, GROUND_Y + 48.0f, p.z), pg_m4_mul(pg_m4_rot_z(wob), pg_m4_translate(0, 0, 0)));
        pg3d_mesh(A.bucket_cap, A.ground, &m, WHITE, 0);
        m = pg_m4_mul(pg_m4_translate(p.x + 52.0f, GROUND_Y + 28.0f, p.z - 6.0f), pg_m4_rot_z(-0.42f));
        pg3d_mesh(A.spade_h, A.wood_v, &m, WHITE, 0);
        m = pg_m4_mul(pg_m4_translate(p.x + 52.0f + sinf(0.42f) * 40.0f, GROUND_Y + 28.0f - cosf(0.42f) * 40.0f, p.z - 6.0f), pg_m4_rot_z(-0.42f));
        pg3d_mesh(A.spade_b, A.btnbase, &m, pg_rgb(255, 150, 140), 0);
        pg3d_blob_shadow(p.x + 20.0f, GROUND_Y + 0.7f, p.z, 62.0f, 0.45f);
        float sx, sy, d;
        if (pg3d_project(pg_v3(p.x, GROUND_Y + 24.0f, p.z), &sx, &sy, &d)) { pokes[P_BUCKET].sx = sx; pokes[P_BUCKET].sy = sy; pokes[P_BUCKET].r = 44; }
    }
    /* three flowers */
    for (int i = 0; i < 3; i++) {
        static const float fx_[3] = { 590, 636, 684 }, fy_[3] = { 520, 527, 518 }, hgt[3] = { 1.0f, 1.25f, 0.9f };
        static const PG_Color petal_col[3] = { { 255, 140, 185, 255 }, { 255, 255, 255, 255 }, { 255, 170, 70, 255 } };
        PG_V3 p = P3(fx_[i], fy_[i], 150.0f);
        float pk = pokes[P_F0 + i].t, sw = 0.10f * sinf(tm * 1.7f + (float)i * 2.0f) + 0.5f * pk * sinf(pk * 20.0f);
        float h = 40.0f * hgt[i];
        m = pg_m4_mul(pg_m4_translate(p.x, GROUND_Y, p.z), pg_m4_mul(pg_m4_rot_z(sw), pg_m4_mul(pg_m4_translate(0, h * 0.5f, 0), pg_m4_scale(1, h / 40.0f, 1))));
        pg3d_mesh(A.stem_m, A.stem, &m, WHITE, 0);
        PG_V3 head = pg_v3(p.x - sinf(sw) * h, GROUND_Y + cosf(sw) * h, p.z + 4.0f);
        float sz = 34.0f * (1.0f + 0.3f * pk);
        pg3d_sprite(A.flower, head, sz, sz, tm * 0.4f * (float)(i - 1) + pk * 4.0f, petal_col[i], 0);
        pg3d_blob_shadow(p.x + 4.0f, GROUND_Y + 0.7f, p.z, 16.0f, 0.4f);
        float sx, sy, d;
        if (pg3d_project(head, &sx, &sy, &d)) { pokes[P_F0 + i].sx = sx; pokes[P_F0 + i].sy = sy; pokes[P_F0 + i].r = 30; }
    }
    /* starfish lying on the beach */
    {
        static const float sxp[2] = { 450, 905 }, syp[2] = { 528, 531 };
        for (int i = 0; i < 2; i++) {
            PG_V3 p = P3(sxp[i], syp[i], 160.0f);
            m = pg_m4_mul(pg_m4_translate(p.x, GROUND_Y + 0.8f, p.z), pg_m4_mul(pg_m4_rot_y(0.6f + (float)i), pg_m4_mul(pg_m4_rot_x(-PG_PI * 0.5f), pg_m4_scale(22, 22, 1))));
            pg3d_mesh(A.decal, A.starfish, &m, WHITE, PG3D_BG | PG3D_TWOSIDED);
        }
    }
}

static void draw_fx_and_fingers(float tm)
{
    for (int i = 0; i < MAX_FX; i++) {
        const Fx *f = &fx[i];
        if (f->life <= 0) continue;
        float k = f->life / f->max, a = k > 0.35f ? 1.0f : k / 0.35f;
        if (f->kind == 0) {
            PG_Color c = pg_hsv(f->hue, 0.5f, 1.0f);
            PG_V3 p = f->p; p.z = fmaxf(p.z, 60.0f);
            pg3d_sprite(pg3d_builtin(PG3D_TEX_STAR), p, f->size * a, f->size * a, f->rot + f->life * 3.0f, c, 0);
            pg3d_sprite(pg3d_builtin(PG3D_TEX_GLOW), p, f->size * 1.8f * a, f->size * 1.8f * a, 0, pg_alpha(c, 0.36f), PG3D_ADDITIVE);
        } else {
            PG_Color c = pg_hsv(f->hue, 0.25f, 1.0f);
            float s = f->size * (1.6f - k * 0.9f);
            pg3d_sprite(A.puff, f->p, s, s, f->rot, pg_alpha(c, 0.55f * k), 0);
        }
    }
    const PG_Touch *t = pg_touches();
    for (int i = 0; i < PG_MAX_TOUCH; i++) {
        if (t[i].state == PG_TOUCH_NONE || t[i].state == PG_TOUCH_UP) continue;
        PG_V3 p;
        if (!pg3d_plane_z(t[i].x, t[i].y, 70.0f, &p)) continue;
        PG_Color c = pg_hsv(fing[i].hue, 0.55f, 1.0f);
        float pulse = 1.0f + 0.08f * sinf(tm * 9.0f + (float)i);
        pg3d_sprite(pg3d_builtin(PG3D_TEX_GLOW), p, 104.0f * pulse, 104.0f * pulse, 0, pg_alpha(c, 0.62f), PG3D_ADDITIVE);
        pg3d_sprite(pg3d_builtin(PG3D_TEX_GLOW), p, 30.0f, 30.0f, 0, pg_rgba(255, 255, 255, 120), PG3D_ADDITIVE);
    }
    /* the ghost hand: shows where and how to pour */
    if (hint_a > 0.02f) {
        PG_V3 p;
        float bob = 4.0f * sinf(tm * 4.0f);
        if (pg3d_plane_z(ghost_px, ghost_py + bob, 96.0f, &p)) {
            float s = 118.0f, ta = clampf(hint_a, 0, 1);
            p.y += s * 0.42f;
            pg3d_sprite(A.hand, p, s, s, 0.0f, pg_rgba(255, 255, 255, (uint8_t)(ta * 255.0f)), 0);
            PG_V3 g = p; g.y -= s * 0.42f; g.z += 4.0f;
            pg3d_sprite(pg3d_builtin(PG3D_TEX_GLOW), g, 110.0f, 110.0f, 0, pg_alpha(pg_hsv(ghost_hue, 0.5f, 1.0f), 0.6f * ta), PG3D_ADDITIVE);
        }
    }
}

static void draw_sheen(void)
{
    PG_M4 m = pg_m4_mul(pg_m4_translate((WX0 + WX1) * 0.5f, (WYT + WYB) * 0.5f, 6.0f), pg_m4_scale((WX1 - WX0) * 0.5f, (WYT - WYB) * 0.5f, 1.0f));
    pg3d_mesh(A.decal, A.sheen, &m, pg_rgba(255, 255, 255, 70), PG3D_ADDITIVE | PG3D_UNLIT | PG3D_TWOSIDED);
}

static void render(SDL_Renderer *r)
{
    const float tm = pg_time();
    PG_Camera cam = pg3d_camera_2d_fov(CAM_FOV);
    cam.eye.x += sway_x; cam.target.x += sway_x;
    cam.eye.y += sway_y; cam.target.y += sway_y;
    pg3d_begin(&cam);
    draw_back(tm);
    pg3d_flush(r);                                   /* everything behind the sand is on the screen now */
    sand_draw(r, win_rx, win_ry);                    /* the sand: one nearest-filtered streaming texture */
    draw_frame();
    draw_crate_and_button(tm);
    draw_decor(tm);
    draw_sheen();
    draw_fx_and_fingers(tm);
    pg3d_end(r);
}

static const PG_Game game = { "sand-pour", "Sand Pour", init, update, render, shutdown };
int main(int argc, char **argv) { return pg_run(&game, argc, argv); }
