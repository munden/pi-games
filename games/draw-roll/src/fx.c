/* fx.c - Draw & Roll: particle pool. Stars twinkle out, confetti flutters, bubbles float up, glows swell. */
#include "fx.h"

#define MAX_P 120

enum { K_FREE = 0, K_STAR, K_CONFETTI, K_BUBBLE, K_GLOW };

typedef struct {
    uint8_t  kind;
    float    x, y, z, vx, vy, vz;
    float    life, max_life, size, rot, spin, grav, drag;
    PG_Color color;
} Particle;

static Particle P[MAX_P];
static int      next_slot;
static float    spawn_scale = 1.0f;
static PG_Tex   t_bubble;

void fx_init(void)
{
    PG_Image *im = pg_img_new(32, 32, 0x00FFFFFFu);
    pg_img_disc(im, 16, 16, 14.5f, PG_ARGB(34, 200, 235, 255));
    pg_img_ring(im, 16, 16, 13.5f, 2.6f, PG_ARGB(190, 235, 250, 255));
    pg_img_disc(im, 10.5f, 10.5f, 3.0f, PG_ARGB(235, 255, 255, 255));
    pg_img_disc(im, 21.0f, 22.0f, 1.6f, PG_ARGB(150, 255, 255, 255));
    t_bubble = tex_finish(im, false);
    for (int i = 0; i < MAX_P; i++) P[i].kind = K_FREE;
}

void fx_shutdown(void) {}

int fx_count(void)
{
    int n = 0;
    for (int i = 0; i < MAX_P; i++) n += P[i].kind != K_FREE;
    return n;
}

static Particle *alloc_p(void)
{
    for (int k = 0; k < MAX_P; k++) {
        int i = (next_slot + k) % MAX_P;
        if (P[i].kind == K_FREE) { next_slot = (i + 1) % MAX_P; return &P[i]; }
    }
    Particle *p = &P[next_slot];                 /* pool full: recycle the next one in line */
    next_slot = (next_slot + 1) % MAX_P;
    return p;
}

static int scaled(int n)
{
    int m = (int)((float)n * spawn_scale + 0.5f);
    return m < 1 ? 1 : m;
}

void fx_burst(float x, float y, float z, int n, float speed, float hue, float spread, float size)
{
    n = scaled(n);
    for (int i = 0; i < n; i++) {
        Particle *p = alloc_p();
        float a = pg_rand_range(0.0f, PG_TAU), s = speed * pg_rand_range(0.35f, 1.0f);
        p->kind = K_STAR; p->x = x; p->y = y; p->z = z;
        p->vx = cosf(a) * s; p->vy = sinf(a) * s + speed * 0.25f; p->vz = pg_rand_range(-20.0f, 40.0f);
        p->max_life = p->life = pg_rand_range(0.7f, 1.25f);
        p->size = size * pg_rand_range(0.7f, 1.2f);
        p->rot = pg_rand_range(0.0f, PG_TAU); p->spin = pg_rand_range(-3.0f, 3.0f);
        p->grav = 320.0f; p->drag = 2.2f;
        p->color = pg_hsv(hue + pg_rand_range(-spread, spread), 0.5f, 1.0f);
    }
}

void fx_confetti(float x, float y, float z, int n, float speed)
{
    n = scaled(n);
    for (int i = 0; i < n; i++) {
        Particle *p = alloc_p();
        float a = pg_rand_range(0.2f, PG_PI - 0.2f), s = speed * pg_rand_range(0.4f, 1.0f);
        p->kind = K_CONFETTI; p->x = x; p->y = y; p->z = z;
        p->vx = cosf(a) * s; p->vy = sinf(a) * s; p->vz = pg_rand_range(-30.0f, 50.0f);
        p->max_life = p->life = pg_rand_range(1.0f, 1.6f);
        p->size = pg_rand_range(7.0f, 11.0f);
        p->rot = pg_rand_range(0.0f, PG_TAU); p->spin = pg_rand_range(-7.0f, 7.0f);
        p->grav = 420.0f; p->drag = 1.6f;
        p->color = pg_hsv(pg_rand_range(0.0f, 360.0f), 0.65f, 1.0f);
    }
}

void fx_bubble(float x, float y, float z, float size, float rise)
{
    Particle *p = alloc_p();
    p->kind = K_BUBBLE; p->x = x; p->y = y; p->z = z;
    p->vx = pg_rand_range(-14.0f, 14.0f); p->vy = rise; p->vz = 0;
    p->max_life = p->life = pg_rand_range(1.5f, 2.6f);
    p->size = size; p->rot = 0; p->spin = pg_rand_range(-0.6f, 0.6f);
    p->grav = -20.0f; p->drag = 0.5f;
    p->color = pg_rgb(255, 255, 255);
}

void fx_glow(float x, float y, float z, float size, PG_Color c, float life)
{
    Particle *p = alloc_p();
    p->kind = K_GLOW; p->x = x; p->y = y; p->z = z;
    p->vx = p->vy = p->vz = 0;
    p->max_life = p->life = life;
    p->size = size; p->rot = 0; p->spin = 0; p->grav = 0; p->drag = 0;
    p->color = c;
}

void fx_update(float degrade)
{
    spawn_scale = 1.0f - 0.6f * clampf(degrade, 0.0f, 1.0f);
    for (int i = 0; i < MAX_P; i++) {
        Particle *p = &P[i];
        if (p->kind == K_FREE) continue;
        p->life -= PG_DT;
        if (p->life <= 0.0f) { p->kind = K_FREE; continue; }
        float dr = 1.0f / (1.0f + p->drag * PG_DT);
        p->vx *= dr; p->vy = (p->vy - p->grav * PG_DT) * dr; p->vz *= dr;
        p->x += p->vx * PG_DT; p->y += p->vy * PG_DT; p->z += p->vz * PG_DT;
        p->rot += p->spin * PG_DT;
        if (p->kind == K_BUBBLE) p->vx += sinf(p->life * 5.0f + p->x) * 6.0f * PG_DT;
    }
}

void fx_render(void)
{
    PG_Tex star = pg3d_builtin(PG3D_TEX_STAR), glow = pg3d_builtin(PG3D_TEX_GLOW), white = pg3d_white();
    for (int i = 0; i < MAX_P; i++) {
        const Particle *p = &P[i];
        if (p->kind == K_FREE) continue;
        float age = p->max_life - p->life;
        float in = clampf(age / 0.10f, 0.0f, 1.0f);                     /* quick pop-in, no flicker */
        float out = smooth01(p->life / (p->max_life * 0.45f));          /* long soft fade-out */
        PG_V3 pos = pg_v3(p->x, p->y, p->z);
        switch (p->kind) {
        case K_STAR: {
            float k = in * (0.35f + 0.65f * out);
            pg3d_sprite(star, pos, p->size * k, p->size * k, p->rot, p->color, 0);
            pg3d_sprite(glow, pos, p->size * 2.2f * k, p->size * 2.2f * k, 0, pg_alpha(p->color, 0.55f * out), PG3D_ADDITIVE);
            break; }
        case K_CONFETTI: {
            float flutter = fabsf(cosf(p->rot * 0.7f)) * 0.8f + 0.2f;   /* tumbling flake */
            float k = in * out;
            pg3d_sprite(white, pos, p->size * k, p->size * 0.55f * k * flutter, p->rot, p->color, 0);
            break; }
        case K_BUBBLE: {
            float grow = 0.8f + 0.35f * (age / p->max_life);
            float k = in * (p->life > 0.25f ? 1.0f : p->life / 0.25f * 1.2f);   /* pops: swells and vanishes */
            pg3d_sprite(t_bubble, pos, p->size * grow * k, p->size * grow * k, 0, pg_rgba(255, 255, 255, (uint8_t)(220.0f * clampf(k, 0, 1))), 0);
            break; }
        case K_GLOW: {
            float t = 1.0f - p->life / p->max_life;
            float k = 0.5f + 0.9f * t;
            pg3d_sprite(glow, pos, p->size * k, p->size * k, 0, pg_alpha(p->color, 0.8f * (1.0f - t) * (1.0f - t)), PG3D_ADDITIVE);
            break; }
        default: break;
        }
    }
}
