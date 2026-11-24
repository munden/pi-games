/* balls.c - Draw & Roll: shiny painted balls.
 *
 * Eight painted patterns share one small texture sheet (64x32 texels each, wrapped around a 12x8 sphere). A ball is a
 * circle body (radius >= 20, restitution 0.6); the 3D sphere spins with the body's angle, squashes along every hit and
 * gets a glossy highlight. The bucket eats balls that fall into its mouth; balls that wander off, rest for long or
 * exceed the cap shrink away gently ("poof") and are recycled by the cloud.
 */
#include "balls.h"
#include "fx.h"
#include "toys.h"

#include <string.h>

#define SLOTS 18
#define NPAT  8

enum { B_FREE = 0, B_ALIVE, B_SWALLOW, B_POOF };

typedef struct {
    uint8_t  state, pattern;
    PG_Body *body;
    float    r, x, y, vx, vy, angle;
    float    anim, squash, sq_ang, still, age;
    uint32_t serial;
} Ball;

static PG_World *world;
static Ball      B[SLOTS];
static PG_Mesh  *m_ball, *m_quad;
static PG_Tex    t_pat[NPAT];
static float     drop_timer;
static uint64_t  last_drop_tick;
static uint32_t  serial_ctr;
static int       pattern_ctr;
static int       cap_now = MAX_BALLS;

/* ---------------------------------------------------------------- patterns */
static void pat_beach(PG_Image *im)
{
    static const uint32_t col[6] = { 0xFF4F6A, 0xFFFFFF, 0xFFC53D, 0xFFFFFF, 0x3AA8FF, 0xFFFFFF };
    for (int i = 0; i < 6; i++) pg_img_rect(im, i * 64 / 6, 0, 64 / 6 + 1, 32, PG_RGB24(col[i]));
    pg_img_rect(im, 0, 0, 64, 3, PG_RGB24(0xFFFFFF));
    pg_img_rect(im, 0, 29, 64, 3, PG_RGB24(0xFFFFFF));
}

static void pat_polka(PG_Image *im)
{
    pg_img_rect(im, 0, 0, 64, 32, PG_RGB24(0xFF7EB6));
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 5; i++) {
            float x = (float)(i * 16) + ((j & 1) ? 8.0f : 0.0f), y = 5.0f + (float)(j * 11);
            pg_img_disc(im, x, y, 3.6f, PG_RGB24(0xFFFFFF));
        }
}

static void pat_stars(PG_Image *im)
{
    pg_img_rect(im, 0, 0, 64, 32, PG_RGB24(0x4C5BFF));
    for (int j = 0; j < 2; j++)
        for (int i = 0; i < 5; i++) {
            float x = (float)(i * 16) + ((j & 1) ? 8.0f : 0.0f), y = 9.0f + (float)(j * 14);
            pg_img_star(im, x, y, 6.8f, 3.0f, 5, 0.0f, PG_RGB24(0xFFD83B));
        }
}

static void pat_rainbow(PG_Image *im)
{
    static const uint32_t col[8] = { 0xFF4F6A, 0xFF9B3D, 0xFFD83B, 0x5BD96B, 0x3CCFE0, 0x3A8CFF, 0x9A6BFF, 0xFF7EC8 };
    for (int i = 0; i < 8; i++) pg_img_rect(im, 0, i * 4, 64, 4, PG_RGB24(col[i]));
}

static void pat_checker(PG_Image *im) { pg_img_checker(im, 8, PG_RGB24(0x8BE04E), PG_RGB24(0xFFFFFF)); }

static void pat_swirl(PG_Image *im) { pg_img_stripes(im, 7, 8, PG_RGB24(0xFF6FA8), PG_RGB24(0xFFF0DC)); }

static void pat_smiley(PG_Image *im)
{
    pg_img_rect(im, 0, 0, 64, 32, PG_RGB24(0xFFD93B));
    pg_img_rect(im, 0, 0, 64, 3, PG_RGB24(0xFFC21F));
    pg_img_rect(im, 0, 29, 64, 3, PG_RGB24(0xFFC21F));
    pg_img_disc(im, 21.0f, 19.0f, 3.2f, PG_ARGB(150, 255, 120, 140));
    pg_img_disc(im, 43.0f, 19.0f, 3.2f, PG_ARGB(150, 255, 120, 140));
    pg_img_disc(im, 27.0f, 12.0f, 2.3f, PG_RGB24(0x5A2A1A));
    pg_img_disc(im, 37.0f, 12.0f, 2.3f, PG_RGB24(0x5A2A1A));
    static const float sx[5] = { 25, 28, 32, 36, 39 }, sy[5] = { 18, 21.5f, 23, 21.5f, 18 };
    for (int i = 0; i < 4; i++) pg_img_line(im, sx[i], sy[i], sx[i + 1], sy[i + 1], 1.9f, PG_RGB24(0x5A2A1A));
}

static void pat_flowers(PG_Image *im)
{
    pg_img_rect(im, 0, 0, 64, 32, PG_RGB24(0xB69CFF));
    static const float fx[5] = { 8, 24, 40, 56, 32 }, fy[5] = { 9, 9, 9, 9, 23 };
    for (int k = 0; k < 5; k++) {
        float cx = fx[k] + (k == 4 ? 0.0f : 0.0f), cy = fy[k];
        for (int p = 0; p < 5; p++) {
            float a = (float)p * PG_TAU / 5.0f;
            pg_img_disc(im, cx + cosf(a) * 3.4f, cy + sinf(a) * 3.4f, 2.5f, PG_RGB24(0xFFFFFF));
        }
        pg_img_disc(im, cx, cy, 2.2f, PG_RGB24(0xFFC93D));
    }
    pg_img_disc(im, 8.0f, 24.0f, 2.2f, PG_RGB24(0xFFFFFF));
    pg_img_disc(im, 56.0f, 24.0f, 2.2f, PG_RGB24(0xFFFFFF));
}

void balls_init(PG_World *w)
{
    world = w;
    m_ball = pg_mesh_sphere(1.0f, 12, 8);
    m_quad = pg_mesh_quad(1.0f, 1.0f);
    PG_Image *sheet = pg_img_new(128, 128, 0xFFFFFFFFu);
    void (*paint[NPAT])(PG_Image *) = { pat_beach, pat_polka, pat_stars, pat_rainbow, pat_checker, pat_swirl, pat_smiley, pat_flowers };
    for (int k = 0; k < NPAT; k++) {
        PG_Image *im = pg_img_new(64, 32, 0xFFFFFFFFu);
        paint[k](im);
        pg_img_noise(im, 4, 100u + (uint32_t)k);
        img_blit(sheet, im, (k & 1) * 64, (k >> 1) * 32);
        pg_img_free(im);
    }
    PG_Tex all = tex_finish(sheet, true);
    for (int k = 0; k < NPAT; k++) t_pat[k] = tex_sub(all, 128, 128, (k & 1) * 64, (k >> 1) * 32, 64, 32);
    memset(B, 0, sizeof B);
    drop_timer = 0.9f;
    serial_ctr = 0; last_drop_tick = 0;
    pattern_ctr = pg_rand_int(0, NPAT - 1);
}

void balls_shutdown(void)
{
    pg_mesh_free(m_ball); pg_mesh_free(m_quad);
    m_ball = m_quad = NULL;
}

bool balls_nearest(float x, float y, float *bx, float *by)
{
    float best = 1e18f; bool found = false;
    for (int i = 0; i < SLOTS; i++) {
        if (B[i].state != B_ALIVE) continue;
        float dx = B[i].x - x, dy = B[i].y - y, d = dx * dx + dy * dy;
        if (d < best) { best = d; *bx = B[i].x; *by = B[i].y; found = true; }
    }
    return found;
}

/* ------------------------------------------------------------------ life cycle */
static void ball_end_body(Ball *b)
{
    if (b->body) { pg_world_remove(world, b->body); b->body = NULL; }
}

static void ball_poof(Ball *b)
{
    if (b->state != B_ALIVE) return;
    ball_end_body(b);
    b->state = B_POOF; b->anim = 0.0f;
    fx_burst(b->x, b->y, 24.0f, 5, 130.0f, 50.0f + (float)b->pattern * 40.0f, 40.0f, 15.0f);
}

static void ball_swallow(Ball *b)
{
    ball_end_body(b);
    b->state = B_SWALLOW; b->anim = 0.0f;
    toys_bucket_eat(b->x, b->y);
}

bool balls_drop(void)
{
    uint64_t now = pg_ticks();
    if (last_drop_tick != 0 && now - last_drop_tick < 9) return false;
    int alive = 0, oldest = -1; uint32_t old_serial = 0xFFFFFFFFu;
    for (int i = 0; i < SLOTS; i++)
        if (B[i].state == B_ALIVE) { alive++; if (B[i].serial < old_serial) { old_serial = B[i].serial; oldest = i; } }
    while (alive >= cap_now && oldest >= 0) {        /* at the cap: the oldest ball poofs to make room */
        ball_poof(&B[oldest]);
        alive--;
        oldest = -1; old_serial = 0xFFFFFFFFu;
        for (int i = 0; i < SLOTS; i++) if (B[i].state == B_ALIVE && B[i].serial < old_serial) { old_serial = B[i].serial; oldest = i; }
    }
    Ball *b = NULL;
    for (int i = 0; i < SLOTS; i++) if (B[i].state == B_FREE) { b = &B[i]; break; }
    if (!b) return false;
    float r = pg_rand_range(BALL_R_MIN, BALL_R_MAX), mx, my;
    toys_dispenser_mouth(&mx, &my);
    PG_Body *body = pg_world_add_circle(world, mx, my - r * 0.4f, r, 1.0f);
    if (!body) return false;
    body->restitution = 0.6f; body->friction = 0.5f; body->lin_damp = 0.02f; body->ang_damp = 0.1f;
    body->vx = pg_rand_range(-70.0f, 70.0f); body->vy = -60.0f; body->w = pg_rand_range(-3.0f, 3.0f);
    body->tag = TAG_BALL; body->user = b;
    memset(b, 0, sizeof *b);
    b->state = B_ALIVE; b->body = body; b->r = r; b->x = body->x; b->y = body->y;
    b->pattern = (uint8_t)(pattern_ctr++ % NPAT);
    b->serial = ++serial_ctr;
    last_drop_tick = now ? now : 1;
    drop_timer = 2.5f + pg_rand_range(-0.2f, 0.4f);
    toys_dispenser_pop();
    return true;
}

void balls_contact(PG_Body *body, float impact, float x, float y)
{
    Ball *b = body ? (Ball *)body->user : NULL;
    if (!b || b->state != B_ALIVE) return;
    float nx = b->x - x, ny = b->y - y, l = sqrtf(nx * nx + ny * ny);
    if (l < 1e-3f) return;
    float q = clampf(impact / 1100.0f, 0.0f, 0.30f);
    if (q > b->squash) { b->squash = q; b->sq_ang = atan2f(ny, nx); }
}

void balls_update(float degrade)
{
    cap_now = MAX_BALLS - (int)(clampf(degrade, 0.0f, 1.0f) * 4.0f);
    drop_timer -= PG_DT;
    if (drop_timer <= 0.0f) balls_drop();
    for (int i = 0; i < SLOTS; i++) {
        Ball *b = &B[i];
        if (b->state == B_FREE) continue;
        b->squash = decay(b->squash, 13.0f, PG_DT);
        b->anim += PG_DT;
        if (b->state == B_ALIVE) {
            PG_Body *p = b->body;
            b->x = p->x; b->y = p->y; b->vx = p->vx; b->vy = p->vy; b->angle = p->angle;
            b->age += PG_DT;
            float sp = p->vx * p->vx + p->vy * p->vy;
            b->still = sp < 18.0f * 18.0f ? b->still + PG_DT : 0.0f;
            if (b->y < RIM_Y - 14.0f && fabsf(b->x) < MOUTH_X + 6.0f) ball_swallow(b);
            else if (b->y < -340.0f || b->y > 520.0f || fabsf(b->x) > 560.0f || b->still > 6.0f || b->age > 50.0f) ball_poof(b);
        } else if (b->state == B_SWALLOW) {
            b->vy -= 900.0f * PG_DT;
            b->x += b->vx * 0.4f * PG_DT; b->y += b->vy * PG_DT;
            if (b->anim > 0.42f) b->state = B_FREE;
        } else if (b->state == B_POOF) {
            if (b->anim > 0.32f) b->state = B_FREE;
        }
    }
}

/* ------------------------------------------------------------------ drawing */
static float ball_scale(const Ball *b)
{
    switch (b->state) {
    case B_ALIVE:   return ease_out_back(b->anim / 0.32f);
    case B_SWALLOW: return 1.0f - 0.45f * smooth01(b->anim / 0.35f);
    default:        { float k = 1.0f - b->anim / 0.32f; return k > 0 ? k * k : 0.0f; }
    }
}

void balls_render_shadows(void)
{
    PG_Tex disc = pg3d_builtin(PG3D_TEX_SHADOW);
    const unsigned fl = PG3D_BG | PG3D_UNLIT | PG3D_NOFOG;
    for (int i = 0; i < SLOTS; i++) {
        const Ball *b = &B[i];
        if (b->state == B_FREE || b->state == B_SWALLOW) continue;
        float s = ball_scale(b) * b->r * 1.55f;
        PG_M4 m = pg_m4_trs2(b->x + 17.0f, b->y - 22.0f, BACK_Z + 1.0f, 0, s, s, 1.0f);
        pg3d_mesh(m_quad, disc, &m, pg_rgba(255, 255, 255, 130), fl);
    }
}

void balls_render(void)
{
    for (int i = 0; i < SLOTS; i++) {
        const Ball *b = &B[i];
        if (b->state == B_FREE) continue;
        float R = b->r * ball_scale(b);
        if (R < 0.5f) continue;
        PG_M4 m = pg_m4_mul(pg_m4_rot_z(b->angle), pg_m4_scale(R, R, R));
        if (b->squash > 0.01f) {
            float q = b->squash;
            m = pg_m4_mul(pg_m4_rot_z(-b->sq_ang), m);
            m = pg_m4_mul(pg_m4_scale(1.0f - q, 1.0f + 0.6f * q, 1.0f + 0.4f * q), m);
            m = pg_m4_mul(pg_m4_rot_z(b->sq_ang), m);
        }
        m = pg_m4_mul(pg_m4_translate(b->x, b->y, 0.0f), m);
        pg3d_mesh(m_ball, t_pat[b->pattern], &m, pg_rgb(255, 255, 255), 0);
        pg3d_shine(pg_v3(b->x, b->y, 0.0f), R, 0.85f);
    }
}
