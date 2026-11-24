/* pg3d.c - PS1-style software-transformed 3D on SDL_RenderGeometry. See pg3d.h. */
#include "pg3d.h"
#include "pg_internal.h"

#include <stdlib.h>
#include <string.h>

#define ATLAS     1024
#define MAXV      8192
#define MAX_MAIN  20000
#define MAX_BG    8000
#define MAX_ADD   6000
#define CHUNK     2048
#define NBUCKET   4096

typedef struct { SDL_Vertex v[3]; float z; } Tri;
typedef struct {
    float x, y, d;          /* view space (d = distance along the view axis) */
    float sx, sy;           /* canvas position, valid when d >= near */
    float r, g, b, a;       /* lit colour 0..1 (kept for clipping) */
    float u, v;             /* atlas coordinates */
    SDL_Color col;          /* lit colour as bytes, ready for SDL */
} TV;

/* ---------------------------------------------------------------- math */
PG_M4 pg_m4_identity(void) { PG_M4 r = {{ 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 }}; return r; }

PG_M4 pg_m4_mul(PG_M4 a, PG_M4 b)
{
    PG_M4 r;
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
            r.m[c * 4 + rr] = s;
        }
    return r;
}

PG_M4 pg_m4_translate(float x, float y, float z) { PG_M4 r = pg_m4_identity(); r.m[12] = x; r.m[13] = y; r.m[14] = z; return r; }
PG_M4 pg_m4_scale(float x, float y, float z)     { PG_M4 r = pg_m4_identity(); r.m[0] = x; r.m[5] = y; r.m[10] = z; return r; }
PG_M4 pg_m4_rot_x(float a) { PG_M4 r = pg_m4_identity(); float c = cosf(a), s = sinf(a); r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c; return r; }
PG_M4 pg_m4_rot_y(float a) { PG_M4 r = pg_m4_identity(); float c = cosf(a), s = sinf(a); r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c; return r; }
PG_M4 pg_m4_rot_z(float a) { PG_M4 r = pg_m4_identity(); float c = cosf(a), s = sinf(a); r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c; return r; }

PG_M4 pg_m4_trs2(float x, float y, float z, float angle, float sx, float sy, float sz)
{
    float c = cosf(angle), s = sinf(angle);
    PG_M4 r = pg_m4_identity();
    r.m[0] = c * sx; r.m[1] = s * sx;
    r.m[4] = -s * sy; r.m[5] = c * sy;
    r.m[10] = sz;
    r.m[12] = x; r.m[13] = y; r.m[14] = z;
    return r;
}

PG_V3 pg_m4_apply(const PG_M4 *m, PG_V3 p)
{
    const float *a = m->m;
    return pg_v3(a[0] * p.x + a[4] * p.y + a[8] * p.z + a[12],
                 a[1] * p.x + a[5] * p.y + a[9] * p.z + a[13],
                 a[2] * p.x + a[6] * p.y + a[10] * p.z + a[14]);
}

/* ---------------------------------------------------------- global state */
static struct {
    PG_Camera cam;
    PG_V3 eye, right, up, fwd;
    float near, focal;
    bool  sky_on, sky_drawn;
    PG_Color sky_top, sky_bot;
    PG_V3 ldir;
    float key[3], amb_up[3], amb_dn[3];
    bool  fog_on; float fog_s, fog_e, fog_c[3];
    int   tris_frame, tris_last;
    bool  warned;
} S;

static double cpu_acc_ms, sdl_acc_ms;                 /* pg3d CPU time this frame, excluding SDL_RenderGeometry */
static inline double now_ms(void) { return (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency(); }
#define CPU_BEGIN() double cpu_t0_ = now_ms()
#define CPU_END()   (cpu_acc_ms += now_ms() - cpu_t0_)

static SDL_Texture *atlas;
static int ax, ay, arow;
static PG_Tex builtin[PG3D_TEX_COUNT], white_tex;

static Tri *bg_t, *main_t, *add_t;
static int  n_bg, n_main, n_add;
static uint16_t *order;
static SDL_Vertex *vbuf;

/* --------------------------------------------------------------- atlas */
PG_Tex pg3d_texture(const PG_Image *img)
{
    if (!atlas) pg_fatal("pg3d_texture called before init");
    int w = img->w + 2, h = img->h + 2;
    if (ax + w > ATLAS) { ax = 0; ay += arow; arow = 0; }
    if (ay + h > ATLAS || w > ATLAS) pg_fatal("texture atlas full (%dx%d); use fewer/smaller textures", ATLAS, ATLAS);
    SDL_Rect r = { ax + 1, ay + 1, img->w, img->h };
    SDL_UpdateTexture(atlas, &r, img->px, img->w * 4);
    PG_Tex t = { ((float)r.x + 0.02f) / ATLAS, ((float)r.y + 0.02f) / ATLAS,
                 ((float)(r.x + r.w) - 0.02f) / ATLAS, ((float)(r.y + r.h) - 0.02f) / ATLAS };
    ax += w; if (h > arow) arow = h;
    return t;
}

PG_Tex pg3d_white(void) { return white_tex; }
PG_Tex pg3d_builtin(int which) { return builtin[which >= 0 && which < PG3D_TEX_COUNT ? which : 0]; }

void pg3d_init(SDL_Renderer *r)
{
    atlas = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, ATLAS, ATLAS);
    if (!atlas) pg_fatal("atlas: %s", SDL_GetError());
    uint32_t *zero = calloc((size_t)ATLAS * ATLAS, 4);
    if (zero) { SDL_UpdateTexture(atlas, NULL, zero, ATLAS * 4); free(zero); }
    SDL_SetTextureBlendMode(atlas, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(atlas, SDL_ScaleModeNearest);
    ax = ay = arow = 0;

    bg_t = malloc(sizeof(Tri) * MAX_BG); main_t = malloc(sizeof(Tri) * MAX_MAIN); add_t = malloc(sizeof(Tri) * MAX_ADD);
    order = malloc(sizeof(uint16_t) * MAX_MAIN); vbuf = malloc(sizeof(SDL_Vertex) * CHUNK * 3);
    if (!bg_t || !main_t || !add_t || !order || !vbuf) pg_fatal("out of memory (pg3d)");
    n_bg = n_main = n_add = 0;

    PG_Image *im = pg_img_new(4, 4, 0xFFFFFFFFu);
    white_tex = pg3d_texture(im); pg_img_free(im);

    im = pg_img_new(64, 64, 0x00FFFFFFu); pg_img_disc(im, 32, 32, 31.5f, 0xFFFFFFFFu);
    builtin[PG3D_TEX_DISC] = pg3d_texture(im); pg_img_free(im);

    im = pg_img_new(64, 64, 0x00000000u);
    for (int j = 0; j < 64; j++) for (int i = 0; i < 64; i++) {
        float d = sqrtf(((float)i - 31.5f) * ((float)i - 31.5f) + ((float)j - 31.5f) * ((float)j - 31.5f)) / 32.0f;
        float a = d >= 1 ? 0 : 1 - d; a = a * a * (3 - 2 * a);
        im->px[j * 64 + i] = PG_ARGB((uint32_t)(a * 255.0f), 0, 0, 0);
    }
    builtin[PG3D_TEX_SHADOW] = pg3d_texture(im); pg_img_free(im);

    im = pg_img_new(64, 64, 0x00FFFFFFu);
    for (int j = 0; j < 64; j++) for (int i = 0; i < 64; i++) {
        float d = sqrtf(((float)i - 31.5f) * ((float)i - 31.5f) + ((float)j - 31.5f) * ((float)j - 31.5f)) / 32.0f;
        float a = d >= 1 ? 0 : (1 - d) * (1 - d);
        im->px[j * 64 + i] = PG_ARGB((uint32_t)(a * 255.0f), 255, 255, 255);
    }
    builtin[PG3D_TEX_GLOW] = pg3d_texture(im); pg_img_free(im);

    im = pg_img_new(64, 64, 0x00FFFFFFu); pg_img_star(im, 32, 33, 30, 13, 5, 0, 0xFFFFFFFFu);
    builtin[PG3D_TEX_STAR] = pg3d_texture(im); pg_img_free(im);

    S.ldir = pg_v3_norm(pg_v3(0.45f, 0.80f, 0.55f));
    pg3d_light(S.ldir, pg_rgb(184, 178, 164), pg_rgb(132, 142, 168), pg_rgb(86, 82, 76));
    S.fog_on = false; S.sky_on = false;
    S.cam = pg3d_camera_2d();
}

void pg3d_shutdown(void)
{
    if (atlas) SDL_DestroyTexture(atlas);
    atlas = NULL;
    free(bg_t); free(main_t); free(add_t); free(order); free(vbuf);
    bg_t = main_t = add_t = NULL; order = NULL; vbuf = NULL;
}

/* -------------------------------------------------------------- camera */
PG_Camera pg3d_camera_2d_fov(float fov)
{
    PG_Camera c;
    float dist = (PG_H * 0.5f) / tanf(fov * 0.5f * PG_PI / 180.0f);
    c.eye = pg_v3(0, 0, dist); c.target = pg_v3(0, 0, 0); c.up = pg_v3(0, 1, 0);
    c.fov_deg = fov; c.near = 20.0f; c.far = 6000.0f;
    return c;
}
PG_Camera pg3d_camera_2d(void) { return pg3d_camera_2d_fov(40.0f); }

void pg3d_light(PG_V3 dir, PG_Color key, PG_Color sky, PG_Color ground)
{
    S.ldir = pg_v3_norm(dir);
    S.key[0] = key.r / 255.0f; S.key[1] = key.g / 255.0f; S.key[2] = key.b / 255.0f;
    S.amb_up[0] = sky.r / 255.0f; S.amb_up[1] = sky.g / 255.0f; S.amb_up[2] = sky.b / 255.0f;
    S.amb_dn[0] = ground.r / 255.0f; S.amb_dn[1] = ground.g / 255.0f; S.amb_dn[2] = ground.b / 255.0f;
}

void pg3d_fog(PG_Color c, float start, float end)
{
    S.fog_on = end > start;
    S.fog_s = start; S.fog_e = end;
    S.fog_c[0] = c.r / 255.0f; S.fog_c[1] = c.g / 255.0f; S.fog_c[2] = c.b / 255.0f;
}

void pg3d_sky(PG_Color top, PG_Color bottom) { S.sky_on = true; S.sky_top = top; S.sky_bot = bottom; }

void pg3d_begin(const PG_Camera *cam)
{
    S.cam = *cam;
    S.eye = cam->eye;
    S.fwd = pg_v3_norm(pg_v3_sub(cam->target, cam->eye));
    S.right = pg_v3_norm(pg_v3_cross(S.fwd, cam->up));
    S.up = pg_v3_cross(S.right, S.fwd);
    S.near = cam->near > 0.5f ? cam->near : 0.5f;
    S.focal = (PG_H * 0.5f) / tanf(cam->fov_deg * 0.5f * PG_PI / 180.0f);
    n_bg = n_main = n_add = 0;
    S.sky_drawn = false;
    S.tris_last = S.tris_frame; S.tris_frame = 0;
    cpu_acc_ms = 0;
}

float pg3d_cpu_ms(void) { return (float)cpu_acc_ms; }

int pg3d_last_tris(void) { return S.tris_last; }

bool pg3d_project(PG_V3 p, float *sx, float *sy, float *depth)
{
    PG_V3 d = pg_v3_sub(p, S.eye);
    float z = pg_v3_dot(d, S.fwd);
    if (z < S.near) return false;
    if (sx) *sx = PG_W * 0.5f + pg_v3_dot(d, S.right) / z * S.focal;
    if (sy) *sy = PG_H * 0.5f - pg_v3_dot(d, S.up) / z * S.focal;
    if (depth) *depth = z;
    return true;
}

void pg3d_ray(float sx, float sy, PG_V3 *origin, PG_V3 *dir)
{
    PG_V3 d = pg_v3_add(S.fwd, pg_v3_add(pg_v3_scale(S.right, (sx - PG_W * 0.5f) / S.focal), pg_v3_scale(S.up, (PG_H * 0.5f - sy) / S.focal)));
    if (origin) *origin = S.eye;
    if (dir) *dir = pg_v3_norm(d);
}

bool pg3d_plane_z(float sx, float sy, float z, PG_V3 *out)
{
    PG_V3 o, d;
    pg3d_ray(sx, sy, &o, &d);
    if (fabsf(d.z) < 1e-6f) return false;
    float t = (z - o.z) / d.z;
    if (t < 0) return false;
    *out = pg_v3_add(o, pg_v3_scale(d, t));
    return true;
}

/* ------------------------------------------------------------ emission */
static inline float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static inline uint8_t to8(float v) { return (uint8_t)(clamp01(v) * 255.0f + 0.5f); }

/* project once per vertex and pack the colour once per vertex: triangles then just copy */
static inline void finish_tv(TV *t)
{
    if (t->d >= S.near) {
        float inv = 1.0f / t->d;
        t->sx = PG_W * 0.5f + t->x * inv * S.focal;
        t->sy = PG_H * 0.5f - t->y * inv * S.focal;
    }
    t->col.r = to8(t->r); t->col.g = to8(t->g); t->col.b = to8(t->b); t->col.a = to8(t->a);
}

static inline void apply_fog(TV *o, unsigned flags)
{
    if (S.fog_on && !(flags & PG3D_NOFOG)) {
        float f = clamp01((o->d - S.fog_s) / (S.fog_e - S.fog_s));
        o->r = o->r * (1 - f) + S.fog_c[0] * f; o->g = o->g * (1 - f) + S.fog_c[1] * f; o->b = o->b * (1 - f) + S.fog_c[2] * f;
    }
}

/* general path: world-space position and (unit) normal */
static TV tv_world(float wx, float wy, float wz, PG_V3 n, PG_Color vc, float tr, float tg, float tb, float ta, float u, float v, PG_Tex tex, unsigned flags)
{
    TV o;
    float dx = wx - S.eye.x, dy = wy - S.eye.y, dz = wz - S.eye.z;
    o.x = dx * S.right.x + dy * S.right.y + dz * S.right.z;
    o.y = dx * S.up.x + dy * S.up.y + dz * S.up.z;
    o.d = dx * S.fwd.x + dy * S.fwd.y + dz * S.fwd.z;
    float lr = 1, lg = 1, lb = 1;
    if (!(flags & PG3D_UNLIT)) {
        float ndl = n.x * S.ldir.x + n.y * S.ldir.y + n.z * S.ldir.z;
        ndl = (flags & PG3D_TWOSIDED) ? fabsf(ndl) : (ndl < 0 ? 0 : ndl);
        float up = n.y * 0.5f + 0.5f;
        lr = S.amb_dn[0] + (S.amb_up[0] - S.amb_dn[0]) * up + S.key[0] * ndl;
        lg = S.amb_dn[1] + (S.amb_up[1] - S.amb_dn[1]) * up + S.key[1] * ndl;
        lb = S.amb_dn[2] + (S.amb_up[2] - S.amb_dn[2]) * up + S.key[2] * ndl;
        lr = lr > 1 ? 1 : lr; lg = lg > 1 ? 1 : lg; lb = lb > 1 ? 1 : lb;
    }
    o.r = vc.r * (1.0f / 255.0f) * tr * lr; o.g = vc.g * (1.0f / 255.0f) * tg * lg; o.b = vc.b * (1.0f / 255.0f) * tb * lb;
    o.a = vc.a * (1.0f / 255.0f) * ta;
    apply_fog(&o, flags);
    o.u = tex.u0 + u * (tex.u1 - tex.u0); o.v = tex.v0 + v * (tex.v1 - tex.v0);
    finish_tv(&o);
    return o;
}

static Tri *alloc_tri(unsigned flags)
{
    Tri *t = NULL;
    if (flags & PG3D_ADDITIVE) { if (n_add < MAX_ADD) t = &add_t[n_add++]; }
    else if (flags & PG3D_BG)  { if (n_bg < MAX_BG) t = &bg_t[n_bg++]; }
    else if (n_main < MAX_MAIN) t = &main_t[n_main++];
    if (!t && !S.warned) { pg_log("pg3d: triangle budget exceeded, dropping geometry (reduce what you draw)"); S.warned = true; }
    return t;
}

static inline void put_vertex(SDL_Vertex *o, const TV *t)
{
    o->position.x = t->sx; o->position.y = t->sy; o->color = t->col; o->tex_coord.x = t->u; o->tex_coord.y = t->v;
}

static void push_tri(const TV *a, const TV *b, const TV *c, unsigned flags)
{
    float area = (b->sx - a->sx) * (c->sy - a->sy) - (b->sy - a->sy) * (c->sx - a->sx);
    if (flags & PG3D_TWOSIDED) { if (area == 0) return; }
    else if (area >= 0) return;                                      /* back-facing (screen y is down) */
    if ((a->sx < 0 && b->sx < 0 && c->sx < 0) || (a->sx > PG_W && b->sx > PG_W && c->sx > PG_W) ||
        (a->sy < 0 && b->sy < 0 && c->sy < 0) || (a->sy > PG_H && b->sy > PG_H && c->sy > PG_H)) return;
    Tri *t = alloc_tri(flags);
    if (!t) return;
    put_vertex(&t->v[0], a); put_vertex(&t->v[1], b); put_vertex(&t->v[2], c);
    t->z = a->d + b->d + c->d;                                       /* sum: same ordering as the mean, no divide */
}

static TV tv_lerp(const TV *p, const TV *q, float t)
{
    TV o;
    o.sx = o.sy = 0;
    o.x = p->x + (q->x - p->x) * t; o.y = p->y + (q->y - p->y) * t; o.d = p->d + (q->d - p->d) * t;
    o.r = p->r + (q->r - p->r) * t; o.g = p->g + (q->g - p->g) * t; o.b = p->b + (q->b - p->b) * t; o.a = p->a + (q->a - p->a) * t;
    o.u = p->u + (q->u - p->u) * t; o.v = p->v + (q->v - p->v) * t;
    finish_tv(&o);
    return o;
}

/* clip against the near plane when needed, then push */
static void emit(const TV *a, const TV *b, const TV *c, unsigned flags)
{
    if (a->d >= S.near && b->d >= S.near && c->d >= S.near) { push_tri(a, b, c, flags); return; }
    if (a->d < S.near && b->d < S.near && c->d < S.near) return;
    const TV *in[3] = { a, b, c };
    TV poly[4]; int n = 0;
    for (int i = 0; i < 3; i++) {
        const TV *p = in[i], *q = in[(i + 1) % 3];
        bool pin = p->d >= S.near, qin = q->d >= S.near;
        if (pin) poly[n++] = *p;
        if (pin != qin) poly[n++] = tv_lerp(p, q, (S.near - p->d) / (q->d - p->d));
    }
    for (int i = 1; i + 1 < n; i++) push_tri(&poly[0], &poly[i], &poly[i + 1], flags);
}

static void mesh_impl(const PG_Mesh *m, PG_Tex tex, const PG_M4 *model, PG_Color tint, unsigned flags)
{
    static TV tv[MAXV];
    if (!m || m->nv > MAXV) return;
    const float *M = model->m;
    const float c0[3] = { M[0], M[1], M[2] }, c1[3] = { M[4], M[5], M[6] }, c2[3] = { M[8], M[9], M[10] };
    const PG_V3 R = S.right, U = S.up, F = S.fwd;
    /* model -> view in one 3x4: view_axis = basis . (M v + t - eye) */
    const float rx0 = R.x * c0[0] + R.y * c0[1] + R.z * c0[2], rx1 = R.x * c1[0] + R.y * c1[1] + R.z * c1[2], rx2 = R.x * c2[0] + R.y * c2[1] + R.z * c2[2];
    const float uy0 = U.x * c0[0] + U.y * c0[1] + U.z * c0[2], uy1 = U.x * c1[0] + U.y * c1[1] + U.z * c1[2], uy2 = U.x * c2[0] + U.y * c2[1] + U.z * c2[2];
    const float fd0 = F.x * c0[0] + F.y * c0[1] + F.z * c0[2], fd1 = F.x * c1[0] + F.y * c1[1] + F.z * c1[2], fd2 = F.x * c2[0] + F.y * c2[1] + F.z * c2[2];
    const float tx = M[12] - S.eye.x, ty = M[13] - S.eye.y, tz = M[14] - S.eye.z;
    const float ox = R.x * tx + R.y * ty + R.z * tz, oy = U.x * tx + U.y * ty + U.z * tz, od = F.x * tx + F.y * ty + F.z * tz;

    /* whole-mesh rejection with a bounding sphere */
    float s0 = sqrtf(c0[0] * c0[0] + c0[1] * c0[1] + c0[2] * c0[2]), s1 = sqrtf(c1[0] * c1[0] + c1[1] * c1[1] + c1[2] * c1[2]), s2 = sqrtf(c2[0] * c2[0] + c2[1] * c2[1] + c2[2] * c2[2]);
    float smax = fmaxf(s0, fmaxf(s1, s2)), smin = fminf(s0, fminf(s1, s2));
    PG_Mesh *mm = (PG_Mesh *)m;
    if (mm->radius <= 0) {
        float r2 = 0;
        for (int i = 0; i < m->nv; i++) { float d2 = m->v[i].x * m->v[i].x + m->v[i].y * m->v[i].y + m->v[i].z * m->v[i].z; if (d2 > r2) r2 = d2; }
        mm->radius = sqrtf(r2) + 0.001f;
    }
    float rad = m->radius * smax;
    if (od + rad < S.near) return;
    if (od > rad * 1.05f) {      /* conservative: magnify by the sphere's NEAREST depth, not its centre depth */
        float inv = S.focal / od, px = PG_W * 0.5f + ox * inv, py = PG_H * 0.5f - oy * inv, pr = rad * S.focal / (od - rad);
        if (px + pr < 0 || px - pr > PG_W || py + pr < 0 || py - pr > PG_H) return;
    }

    const float tr = tint.r * (1.0f / 255.0f), tg = tint.g * (1.0f / 255.0f), tb = tint.b * (1.0f / 255.0f), ta = tint.a * (1.0f / 255.0f);
    const float du = tex.u1 - tex.u0, dv = tex.v1 - tex.v0;
    const bool lit = !(flags & PG3D_UNLIT), two = (flags & PG3D_TWOSIDED) != 0;
    const bool fog = S.fog_on && !(flags & PG3D_NOFOG);
    const bool uniform = (smax - smin) < 0.002f * smax && smax > 1e-6f;

    /* uniform scale: light the vertex normal in MODEL space (no per-vertex normal transform or normalise) */
    float Lm0 = 0, Lm1 = 0, Lm2 = 0, Um0 = 0, Um1 = 0, Um2 = 0;
    if (lit && uniform) {
        float is = 1.0f / s0;
        Lm0 = (c0[0] * S.ldir.x + c0[1] * S.ldir.y + c0[2] * S.ldir.z) * is;
        Lm1 = (c1[0] * S.ldir.x + c1[1] * S.ldir.y + c1[2] * S.ldir.z) * is;
        Lm2 = (c2[0] * S.ldir.x + c2[1] * S.ldir.y + c2[2] * S.ldir.z) * is;
        Um0 = c0[1] * is; Um1 = c1[1] * is; Um2 = c2[1] * is;
    }
    const float kr = S.key[0], kg = S.key[1], kb = S.key[2];
    const float dr = S.amb_dn[0], dg = S.amb_dn[1], db = S.amb_dn[2];
    const float sr = S.amb_up[0] - dr, sg = S.amb_up[1] - dg, sb = S.amb_up[2] - db;
    const float near = S.near, focal = S.focal;

    for (int i = 0; i < m->nv; i++) {
        const PG_Vertex *v = &m->v[i];
        TV *o = &tv[i];
        o->x = rx0 * v->x + rx1 * v->y + rx2 * v->z + ox;
        o->y = uy0 * v->x + uy1 * v->y + uy2 * v->z + oy;
        o->d = fd0 * v->x + fd1 * v->y + fd2 * v->z + od;
        float lr = 1, lg = 1, lb = 1;
        if (lit) {
            float ndl, up;
            if (uniform) {
                ndl = v->nx * Lm0 + v->ny * Lm1 + v->nz * Lm2;
                up = 0.5f * (v->nx * Um0 + v->ny * Um1 + v->nz * Um2) + 0.5f;
            } else {
                float wx = c0[0] * v->nx + c1[0] * v->ny + c2[0] * v->nz, wy = c0[1] * v->nx + c1[1] * v->ny + c2[1] * v->nz, wz = c0[2] * v->nx + c1[2] * v->ny + c2[2] * v->nz;
                float l2 = wx * wx + wy * wy + wz * wz, il = l2 > 1e-12f ? 1.0f / sqrtf(l2) : 1.0f;
                ndl = (wx * S.ldir.x + wy * S.ldir.y + wz * S.ldir.z) * il;
                up = wy * il * 0.5f + 0.5f;
            }
            ndl = two ? fabsf(ndl) : (ndl < 0 ? 0 : ndl);
            lr = dr + sr * up + kr * ndl; lg = dg + sg * up + kg * ndl; lb = db + sb * up + kb * ndl;
            lr = lr > 1 ? 1 : lr; lg = lg > 1 ? 1 : lg; lb = lb > 1 ? 1 : lb;
        }
        o->r = v->c.r * (1.0f / 255.0f) * tr * lr; o->g = v->c.g * (1.0f / 255.0f) * tg * lg; o->b = v->c.b * (1.0f / 255.0f) * tb * lb;
        o->a = v->c.a * (1.0f / 255.0f) * ta;
        if (fog) apply_fog(o, flags);
        o->u = tex.u0 + v->u * du; o->v = tex.v0 + v->v * dv;
        if (o->d >= near) { float inv = 1.0f / o->d; o->sx = PG_W * 0.5f + o->x * inv * focal; o->sy = PG_H * 0.5f - o->y * inv * focal; }
        o->col.r = to8(o->r); o->col.g = to8(o->g); o->col.b = to8(o->b); o->col.a = to8(o->a);
    }
    for (int i = 0; i + 2 < m->ni; i += 3) emit(&tv[m->idx[i]], &tv[m->idx[i + 1]], &tv[m->idx[i + 2]], flags);
}

static void tri_impl(PG_Tex tex, const PG_Vertex *a, const PG_Vertex *b, const PG_Vertex *c, unsigned flags)
{
    const PG_Vertex *vs[3] = { a, b, c };
    TV tv[3];
    for (int i = 0; i < 3; i++) {
        const PG_Vertex *v = vs[i];
        PG_V3 n = pg_v3(v->nx, v->ny, v->nz);
        tv[i] = tv_world(v->x, v->y, v->z, n, v->c, 1, 1, 1, 1, v->u, v->v, tex, flags);
    }
    emit(&tv[0], &tv[1], &tv[2], flags);
}

static void sprite_internal(PG_Tex tex, PG_V3 pos, float w, float h, float roll, PG_Color tint, unsigned flags, float bias)
{
    PG_V3 d = pg_v3_sub(pos, S.eye);
    float depth = pg_v3_dot(d, S.fwd);
    if (depth - bias < S.near) return;
    float inv = 1.0f / depth;
    float cx = PG_W * 0.5f + pg_v3_dot(d, S.right) * inv * S.focal;
    float cy = PG_H * 0.5f - pg_v3_dot(d, S.up) * inv * S.focal;
    float k = S.focal * inv, hw = w * 0.5f * k, hh = h * 0.5f * k;
    if (cx + hw + hh < 0 || cx - hw - hh > PG_W || cy + hw + hh < 0 || cy - hw - hh > PG_H) return;
    float r = tint.r * (1.0f / 255.0f), g = tint.g * (1.0f / 255.0f), b = tint.b * (1.0f / 255.0f), a = tint.a * (1.0f / 255.0f);
    if (S.fog_on && !(flags & (PG3D_NOFOG | PG3D_ADDITIVE))) {
        float f = clamp01((depth - S.fog_s) / (S.fog_e - S.fog_s));
        r = r * (1 - f) + S.fog_c[0] * f; g = g * (1 - f) + S.fog_c[1] * f; b = b * (1 - f) + S.fog_c[2] * f;
    }
    SDL_Color col = { to8(r), to8(g), to8(b), to8(a) };
    float cs = 1, sn = 0;
    if (roll != 0) { cs = cosf(roll); sn = sinf(roll); }
    SDL_Vertex vv[4];
    static const float ux[4] = { 0, 1, 1, 0 }, uy[4] = { 0, 0, 1, 1 }, qx[4] = { -1, 1, 1, -1 }, qy[4] = { -1, -1, 1, 1 };
    for (int i = 0; i < 4; i++) {
        float lx = qx[i] * hw, ly = qy[i] * hh;
        vv[i].position.x = cx + lx * cs - ly * sn; vv[i].position.y = cy + lx * sn + ly * cs;
        vv[i].color = col;
        vv[i].tex_coord.x = ux[i] ? tex.u1 : tex.u0; vv[i].tex_coord.y = uy[i] ? tex.v1 : tex.v0;
    }
    static const int ix[6] = { 0, 1, 2, 0, 2, 3 };
    for (int q = 0; q < 2; q++) {
        Tri *t = alloc_tri(flags);
        if (!t) return;
        for (int kk = 0; kk < 3; kk++) t->v[kk] = vv[ix[q * 3 + kk]];
        t->z = (depth - bias) * 3.0f;
    }
}

static void shadow_impl(float x, float floor_y, float z, float radius, float alpha)
{
    PG_Tex tex = builtin[PG3D_TEX_SHADOW];
    static const float cx[4] = { -1, 1, 1, -1 }, cz[4] = { 1, 1, -1, -1 }, u[4] = { 0, 1, 1, 0 }, v[4] = { 1, 1, 0, 0 };
    TV t[4];
    unsigned fl = PG3D_BG | PG3D_UNLIT | PG3D_TWOSIDED | PG3D_NOFOG;
    for (int i = 0; i < 4; i++)
        t[i] = tv_world(x + cx[i] * radius, floor_y, z + cz[i] * radius, pg_v3(0, 1, 0), pg_rgb(255, 255, 255), 1, 1, 1, alpha, u[i], v[i], tex, fl);
    emit(&t[0], &t[1], &t[2], fl);
    emit(&t[0], &t[2], &t[3], fl);
}

static void shine_impl(PG_V3 c, float radius, float strength)
{
    PG_V3 toc = pg_v3_norm(pg_v3_sub(S.eye, c));
    PG_V3 h = pg_v3_norm(pg_v3_add(S.ldir, toc));
    if (pg_v3_dot(h, toc) < 0.25f) return;
    PG_V3 p = pg_v3_add(c, pg_v3_scale(h, radius * 0.86f));
    sprite_internal(builtin[PG3D_TEX_GLOW], p, radius * 0.95f, radius * 0.95f, 0, pg_rgba(255, 255, 255, to8(strength)), 0, radius * 0.5f);
}

void pg3d_mesh(const PG_Mesh *m, PG_Tex tex, const PG_M4 *model, PG_Color tint, unsigned flags) { CPU_BEGIN(); mesh_impl(m, tex, model, tint, flags); CPU_END(); }
void pg3d_tri(PG_Tex tex, const PG_Vertex *a, const PG_Vertex *b, const PG_Vertex *c, unsigned flags) { CPU_BEGIN(); tri_impl(tex, a, b, c, flags); CPU_END(); }
void pg3d_sprite(PG_Tex tex, PG_V3 pos, float w, float h, float roll, PG_Color tint, unsigned flags) { CPU_BEGIN(); sprite_internal(tex, pos, w, h, roll, tint, flags, 0.0f); CPU_END(); }
void pg3d_blob_shadow(float x, float floor_y, float z, float radius, float alpha) { CPU_BEGIN(); shadow_impl(x, floor_y, z, radius, alpha); CPU_END(); }
void pg3d_shine(PG_V3 c, float radius, float strength) { CPU_BEGIN(); shine_impl(c, radius, strength); CPU_END(); }

/* ------------------------------------------------------------- drawing */
static void sort_far_to_near(const Tri *t, int n)
{
    static int count[NBUCKET + 1];
    if (n <= 0) return;
    float zmin = t[0].z, zmax = t[0].z;
    for (int i = 1; i < n; i++) { if (t[i].z < zmin) zmin = t[i].z; if (t[i].z > zmax) zmax = t[i].z; }
    float span = zmax - zmin;
    if (span < 1e-4f) { for (int i = 0; i < n; i++) order[i] = (uint16_t)i; return; }
    float k = (NBUCKET - 1) / span;
    memset(count, 0, sizeof count);
    for (int i = 0; i < n; i++) count[(int)((zmax - t[i].z) * k) + 1]++;
    for (int i = 0; i < NBUCKET; i++) count[i + 1] += count[i];
    for (int i = 0; i < n; i++) order[count[(int)((zmax - t[i].z) * k)]++] = (uint16_t)i;
}

static void draw_list(SDL_Renderer *r, const Tri *tris, int n, const uint16_t *ord)
{
    for (int i = 0; i < n; ) {
        int c = n - i < CHUNK ? n - i : CHUNK;
        for (int k = 0; k < c; k++) memcpy(&vbuf[k * 3], tris[ord ? ord[i + k] : i + k].v, sizeof(SDL_Vertex) * 3);
        double t_sdl = now_ms();
        SDL_RenderGeometry(r, atlas, vbuf, c * 3, NULL, 0);
        sdl_acc_ms += now_ms() - t_sdl;
        i += c;
    }
}

void pg3d_flush(SDL_Renderer *r)
{
    double flush_t0 = now_ms();
    sdl_acc_ms = 0;
    if (S.sky_on && !S.sky_drawn) {
        SDL_Vertex v[4];
        SDL_Color t = { S.sky_top.r, S.sky_top.g, S.sky_top.b, 255 }, b = { S.sky_bot.r, S.sky_bot.g, S.sky_bot.b, 255 };
        v[0].position = (SDL_FPoint){ 0, 0 };       v[0].color = t; v[0].tex_coord = (SDL_FPoint){ 0, 0 };
        v[1].position = (SDL_FPoint){ PG_W, 0 };    v[1].color = t; v[1].tex_coord = (SDL_FPoint){ 0, 0 };
        v[2].position = (SDL_FPoint){ PG_W, PG_H }; v[2].color = b; v[2].tex_coord = (SDL_FPoint){ 0, 0 };
        v[3].position = (SDL_FPoint){ 0, PG_H };    v[3].color = b; v[3].tex_coord = (SDL_FPoint){ 0, 0 };
        static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        double t_sky = now_ms();
        SDL_RenderGeometry(r, NULL, v, 4, idx, 6);
        sdl_acc_ms += now_ms() - t_sky;
        S.sky_drawn = true;
    }
    SDL_SetTextureBlendMode(atlas, SDL_BLENDMODE_BLEND);
    draw_list(r, bg_t, n_bg, NULL);
    sort_far_to_near(main_t, n_main);
    draw_list(r, main_t, n_main, order);
    if (n_add) {
        SDL_SetTextureBlendMode(atlas, SDL_BLENDMODE_ADD);
        draw_list(r, add_t, n_add, NULL);
        SDL_SetTextureBlendMode(atlas, SDL_BLENDMODE_BLEND);
    }
    S.tris_frame += n_bg + n_main + n_add;
    n_bg = n_main = n_add = 0;
    cpu_acc_ms += (now_ms() - flush_t0) - sdl_acc_ms;
}

void pg3d_end(SDL_Renderer *r) { pg3d_flush(r); }

/* ------------------------------------------------------------- meshes */
PG_Mesh *pg_mesh_new(int nv, int ni)
{
    PG_Mesh *m = calloc(1, sizeof *m);
    if (!m) pg_fatal("out of memory (mesh)");
    m->v = calloc((size_t)nv, sizeof(PG_Vertex));
    m->idx = calloc((size_t)ni, sizeof(uint16_t));
    if (!m->v || !m->idx) pg_fatal("out of memory (mesh)");
    m->nv = nv; m->ni = ni;
    return m;
}

void pg_mesh_free(PG_Mesh *m) { if (m) { free(m->v); free(m->idx); free(m); } }

PG_Mesh *pg_mesh_build(const PG_Vertex *verts, int nv, const uint16_t *idx, int ni)
{
    PG_Mesh *m = pg_mesh_new(nv, ni);
    memcpy(m->v, verts, (size_t)nv * sizeof(PG_Vertex));
    memcpy(m->idx, idx, (size_t)ni * sizeof(uint16_t));
    return m;
}

typedef struct { PG_Mesh *m; int nv, ni; } B;

static int bv(B *b, PG_V3 p, PG_V3 n, float u, float v)
{
    PG_Vertex *o = &b->m->v[b->nv];
    o->x = p.x; o->y = p.y; o->z = p.z; o->nx = n.x; o->ny = n.y; o->nz = n.z; o->u = u; o->v = v; o->c = pg_rgb(255, 255, 255);
    return b->nv++;
}

/* add a triangle, flipping it if its winding disagrees with the vertex normals; skip degenerate ones */
static void bt(B *b, int i0, int i1, int i2)
{
    const PG_Vertex *a = &b->m->v[i0], *c = &b->m->v[i1], *d = &b->m->v[i2];
    PG_V3 e1 = pg_v3(c->x - a->x, c->y - a->y, c->z - a->z), e2 = pg_v3(d->x - a->x, d->y - a->y, d->z - a->z);
    PG_V3 g = pg_v3_cross(e1, e2);
    if (pg_v3_dot(g, g) < 1e-14f) return;
    PG_V3 n = pg_v3((a->nx + c->nx + d->nx), (a->ny + c->ny + d->ny), (a->nz + c->nz + d->nz));
    if (pg_v3_dot(g, n) < 0) { int t = i1; i1 = i2; i2 = t; }
    b->m->idx[b->ni++] = (uint16_t)i0; b->m->idx[b->ni++] = (uint16_t)i1; b->m->idx[b->ni++] = (uint16_t)i2;
}

static void bquad(B *b, PG_V3 p0, PG_V3 p1, PG_V3 p2, PG_V3 p3, PG_V3 n, const float uv[8])
{
    int a = bv(b, p0, n, uv[0], uv[1]), c = bv(b, p1, n, uv[2], uv[3]), d = bv(b, p2, n, uv[4], uv[5]), e = bv(b, p3, n, uv[6], uv[7]);
    bt(b, a, c, d); bt(b, a, d, e);
}

static PG_Mesh *finish(B *b)
{
    b->m->nv = b->nv; b->m->ni = b->ni;
    return b->m;
}

PG_Mesh *pg_mesh_quad(float hw, float hh)
{
    B b = { pg_mesh_new(4, 6), 0, 0 };
    static const float uv[8] = { 0, 1, 1, 1, 1, 0, 0, 0 };
    bquad(&b, pg_v3(-hw, -hh, 0), pg_v3(hw, -hh, 0), pg_v3(hw, hh, 0), pg_v3(-hw, hh, 0), pg_v3(0, 0, 1), uv);
    return finish(&b);
}

PG_Mesh *pg_mesh_floor(float hx, float hz, int dx, int dz)
{
    dx = dx < 1 ? 1 : dx; dz = dz < 1 ? 1 : dz;
    B b = { pg_mesh_new((dx + 1) * (dz + 1), dx * dz * 6), 0, 0 };
    for (int j = 0; j <= dz; j++)
        for (int i = 0; i <= dx; i++) {
            float fu = (float)i / (float)dx, fv = (float)j / (float)dz;
            bv(&b, pg_v3(-hx + 2 * hx * fu, 0, -hz + 2 * hz * fv), pg_v3(0, 1, 0), fu, fv);
        }
    for (int j = 0; j < dz; j++)
        for (int i = 0; i < dx; i++) {
            int a = j * (dx + 1) + i, c = a + 1, d = a + dx + 1, e = d + 1;
            bt(&b, a, d, e); bt(&b, a, e, c);
        }
    return finish(&b);
}

PG_Mesh *pg_mesh_box(float hx, float hy, float hz)
{
    B b = { pg_mesh_new(24, 36), 0, 0 };
    static const float uv[8] = { 0, 1, 1, 1, 1, 0, 0, 0 };
    bquad(&b, pg_v3(-hx, -hy, hz),  pg_v3(hx, -hy, hz),  pg_v3(hx, hy, hz),  pg_v3(-hx, hy, hz),  pg_v3(0, 0, 1), uv);     /* +z */
    bquad(&b, pg_v3(hx, -hy, -hz),  pg_v3(-hx, -hy, -hz),pg_v3(-hx, hy, -hz),pg_v3(hx, hy, -hz),  pg_v3(0, 0, -1), uv);    /* -z */
    bquad(&b, pg_v3(hx, -hy, hz),   pg_v3(hx, -hy, -hz), pg_v3(hx, hy, -hz), pg_v3(hx, hy, hz),   pg_v3(1, 0, 0), uv);     /* +x */
    bquad(&b, pg_v3(-hx, -hy, -hz), pg_v3(-hx, -hy, hz), pg_v3(-hx, hy, hz), pg_v3(-hx, hy, -hz), pg_v3(-1, 0, 0), uv);    /* -x */
    bquad(&b, pg_v3(-hx, hy, hz),   pg_v3(hx, hy, hz),   pg_v3(hx, hy, -hz), pg_v3(-hx, hy, -hz), pg_v3(0, 1, 0), uv);     /* +y */
    bquad(&b, pg_v3(-hx, -hy, -hz), pg_v3(hx, -hy, -hz), pg_v3(hx, -hy, hz), pg_v3(-hx, -hy, hz), pg_v3(0, -1, 0), uv);    /* -y */
    return finish(&b);
}

/* UV for a chamfer/corner vertex: project onto the box face its normal points at most toward. */
static void box_uv(PG_V3 p, PG_V3 n, PG_V3 h, float *u, float *v)
{
    float ax_ = fabsf(n.x), ay_ = fabsf(n.y), az_ = fabsf(n.z), r, up, hr, hu;
    if (az_ >= ax_ && az_ >= ay_)      { r = (n.z > 0 ? 1 : -1) * p.x; up = p.y; hr = h.x; hu = h.y; }
    else if (ax_ >= ay_)               { r = (n.x > 0 ? -1 : 1) * p.z; up = p.y; hr = h.z; hu = h.y; }
    else                               { r = p.x; up = (n.y > 0 ? -1 : 1) * p.z; hr = h.x; hu = h.z; }
    *u = clamp01(0.5f + r / (2 * hr)); *v = clamp01(0.5f - up / (2 * hu));
}

PG_Mesh *pg_mesh_bevel_box(float hx, float hy, float hz, float bev)
{
    float mn = fminf(hx, fminf(hy, hz));
    if (bev > mn * 0.9f) bev = mn * 0.9f;
    if (bev <= 0.01f) return pg_mesh_box(hx, hy, hz);
    B b = { pg_mesh_new(96, 132), 0, 0 };
    PG_V3 h = pg_v3(hx, hy, hz);
    float hh[3] = { hx, hy, hz };
    /* 6 inset faces */
    for (int a = 0; a < 3; a++)
        for (int s = -1; s <= 1; s += 2) {
            int u = (a + 1) % 3, w = (a + 2) % 3;
            float c[4][3];
            static const int su[4] = { -1, 1, 1, -1 }, sw[4] = { -1, -1, 1, 1 };
            PG_V3 pts[4]; float uvs[8];
            PG_V3 n = { 0, 0, 0 }; ((float *)&n)[a] = (float)s;
            for (int k = 0; k < 4; k++) {
                c[k][a] = s * hh[a]; c[k][u] = su[k] * (hh[u] - bev); c[k][w] = sw[k] * (hh[w] - bev);
                pts[k] = pg_v3(c[k][0], c[k][1], c[k][2]);
                box_uv(pts[k], n, h, &uvs[k * 2], &uvs[k * 2 + 1]);
            }
            bquad(&b, pts[0], pts[1], pts[2], pts[3], n, uvs);
        }
    /* 12 edge chamfers */
    for (int a = 0; a < 3; a++)
        for (int bb = a + 1; bb < 3; bb++) {
            int c = 3 - a - bb;
            for (int sa = -1; sa <= 1; sa += 2)
                for (int sb = -1; sb <= 1; sb += 2) {
                    float p[4][3];
                    PG_V3 pts[4]; float uvs[8];
                    PG_V3 n = { 0, 0, 0 }; ((float *)&n)[a] = (float)sa * 0.70710678f; ((float *)&n)[bb] = (float)sb * 0.70710678f;
                    for (int k = 0; k < 4; k++) {
                        int end = (k == 1 || k == 2) ? 1 : -1, onL2 = (k >= 2);
                        p[k][c] = end * (hh[c] - bev);
                        if (!onL2) { p[k][a] = sa * hh[a]; p[k][bb] = sb * (hh[bb] - bev); }
                        else       { p[k][a] = sa * (hh[a] - bev); p[k][bb] = sb * hh[bb]; }
                        pts[k] = pg_v3(p[k][0], p[k][1], p[k][2]);
                        box_uv(pts[k], n, h, &uvs[k * 2], &uvs[k * 2 + 1]);
                    }
                    bquad(&b, pts[0], pts[1], pts[2], pts[3], n, uvs);
                }
        }
    /* 8 corner triangles */
    for (int sx = -1; sx <= 1; sx += 2) for (int sy = -1; sy <= 1; sy += 2) for (int sz = -1; sz <= 1; sz += 2) {
        PG_V3 n = pg_v3_norm(pg_v3((float)sx, (float)sy, (float)sz));
        PG_V3 p0 = pg_v3(sx * hx, sy * (hy - bev), sz * (hz - bev));
        PG_V3 p1 = pg_v3(sx * (hx - bev), sy * hy, sz * (hz - bev));
        PG_V3 p2 = pg_v3(sx * (hx - bev), sy * (hy - bev), sz * hz);
        float u, v;
        box_uv(p0, n, h, &u, &v); int i0 = bv(&b, p0, n, u, v);
        box_uv(p1, n, h, &u, &v); int i1 = bv(&b, p1, n, u, v);
        box_uv(p2, n, h, &u, &v); int i2 = bv(&b, p2, n, u, v);
        bt(&b, i0, i1, i2);
    }
    return finish(&b);
}

PG_Mesh *pg_mesh_sphere(float r, int segs, int rings)
{
    segs = segs < 3 ? 3 : segs; rings = rings < 2 ? 2 : rings;
    B b = { pg_mesh_new((segs + 1) * (rings + 1), segs * rings * 6), 0, 0 };
    for (int j = 0; j <= rings; j++) {
        float th = PG_PI * (float)j / (float)rings;
        for (int i = 0; i <= segs; i++) {
            float ph = -PG_PI + PG_TAU * (float)i / (float)segs;
            PG_V3 n = pg_v3(sinf(th) * sinf(ph), cosf(th), sinf(th) * cosf(ph));
            bv(&b, pg_v3_scale(n, r), n, (float)i / (float)segs, (float)j / (float)rings);
        }
    }
    for (int j = 0; j < rings; j++)
        for (int i = 0; i < segs; i++) {
            int a = j * (segs + 1) + i, c = a + 1, d = a + segs + 1, e = d + 1;
            bt(&b, a, d, e); bt(&b, a, e, c);
        }
    return finish(&b);
}

PG_Mesh *pg_mesh_cylinder(float r, float hh, int segs)
{
    if (segs < 3) segs = 3;
    B b = { pg_mesh_new((segs + 1) * 2 + 2 * (segs + 2), segs * 6 + segs * 6), 0, 0 };
    for (int i = 0; i <= segs; i++) {
        float a = PG_TAU * (float)i / (float)segs, c = cosf(a), s = sinf(a);
        bv(&b, pg_v3(r * c, hh, r * s), pg_v3(c, 0, s), (float)i / (float)segs, 0);
        bv(&b, pg_v3(r * c, -hh, r * s), pg_v3(c, 0, s), (float)i / (float)segs, 1);
    }
    for (int i = 0; i < segs; i++) { int a = i * 2; bt(&b, a, a + 1, a + 3); bt(&b, a, a + 3, a + 2); }
    for (int cap = 0; cap < 2; cap++) {
        float y = cap ? -hh : hh; PG_V3 n = pg_v3(0, cap ? -1.0f : 1.0f, 0);
        int center = bv(&b, pg_v3(0, y, 0), n, 0.5f, 0.5f), first = b.nv;
        for (int i = 0; i <= segs; i++) {
            float a = PG_TAU * (float)i / (float)segs, c = cosf(a), s = sinf(a);
            bv(&b, pg_v3(r * c, y, r * s), n, 0.5f + 0.5f * c, 0.5f + 0.5f * s);
        }
        for (int i = 0; i < segs; i++) bt(&b, center, first + i, first + i + 1);
    }
    return finish(&b);
}

PG_Mesh *pg_mesh_cone(float r, float hh, int segs)
{
    if (segs < 3) segs = 3;
    B b = { pg_mesh_new(segs * 3 + 1 + (segs + 1), segs * 6), 0, 0 };
    for (int i = 0; i < segs; i++) {
        float a0 = PG_TAU * (float)i / (float)segs, a1 = PG_TAU * (float)(i + 1) / (float)segs, am = (a0 + a1) * 0.5f;
        PG_V3 na = pg_v3_norm(pg_v3(cosf(a0) * 2 * hh, r, sinf(a0) * 2 * hh)), nb = pg_v3_norm(pg_v3(cosf(a1) * 2 * hh, r, sinf(a1) * 2 * hh));
        PG_V3 nm = pg_v3_norm(pg_v3(cosf(am) * 2 * hh, r, sinf(am) * 2 * hh));
        int t = bv(&b, pg_v3(0, hh, 0), nm, 0.5f, 0);
        int p = bv(&b, pg_v3(r * cosf(a0), -hh, r * sinf(a0)), na, (float)i / (float)segs, 1);
        int q = bv(&b, pg_v3(r * cosf(a1), -hh, r * sinf(a1)), nb, (float)(i + 1) / (float)segs, 1);
        bt(&b, t, p, q);
    }
    int center = bv(&b, pg_v3(0, -hh, 0), pg_v3(0, -1, 0), 0.5f, 0.5f), first = b.nv;
    for (int i = 0; i <= segs; i++) {
        float a = PG_TAU * (float)i / (float)segs;
        bv(&b, pg_v3(r * cosf(a), -hh, r * sinf(a)), pg_v3(0, -1, 0), 0.5f + 0.5f * cosf(a), 0.5f + 0.5f * sinf(a));
    }
    for (int i = 0; i < segs; i++) bt(&b, center, first + i, first + i + 1);
    return finish(&b);
}

PG_Mesh *pg_mesh_torus(float R, float r, int segs, int sides)
{
    segs = segs < 3 ? 3 : segs; sides = sides < 3 ? 3 : sides;
    B b = { pg_mesh_new((segs + 1) * (sides + 1), segs * sides * 6), 0, 0 };
    for (int i = 0; i <= segs; i++) {
        float a = PG_TAU * (float)i / (float)segs, ca = cosf(a), sa = sinf(a);
        for (int j = 0; j <= sides; j++) {
            float t = PG_TAU * (float)j / (float)sides, ct = cosf(t), st = sinf(t);
            PG_V3 n = pg_v3(ct * ca, st, ct * sa);
            bv(&b, pg_v3((R + r * ct) * ca, r * st, (R + r * ct) * sa), n, (float)i / (float)segs, (float)j / (float)sides);
        }
    }
    for (int i = 0; i < segs; i++)
        for (int j = 0; j < sides; j++) {
            int a = i * (sides + 1) + j, c = a + 1, d = a + sides + 1, e = d + 1;
            bt(&b, a, d, e); bt(&b, a, e, c);
        }
    return finish(&b);
}

void pg_mesh_tint(PG_Mesh *m, PG_Color c)
{
    for (int i = 0; i < m->nv; i++) {
        PG_Color *o = &m->v[i].c;
        o->r = (uint8_t)(o->r * c.r / 255); o->g = (uint8_t)(o->g * c.g / 255); o->b = (uint8_t)(o->b * c.b / 255); o->a = (uint8_t)(o->a * c.a / 255);
    }
}

void pg_mesh_gradient_y(PG_Mesh *m, PG_Color bottom, PG_Color top)
{
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < m->nv; i++) { if (m->v[i].y < lo) lo = m->v[i].y; if (m->v[i].y > hi) hi = m->v[i].y; }
    for (int i = 0; i < m->nv; i++) m->v[i].c = pg_color_lerp(bottom, top, hi > lo ? (m->v[i].y - lo) / (hi - lo) : 0.5f);
}
