/* util.c - Draw & Roll: texture upload helpers, mesh builder, rate-limited sound. */
#include "util.h"

/* --------------------------------------------------------------------- view */
static PG_V3 view_eye = { 0.0f, 0.0f, 668.0f };

void view_set(const PG_Camera *cam) { view_eye = cam->eye; }

PG_V3 at_depth(float x, float y, float z)
{
    float k = (view_eye.z - z) / view_eye.z;          /* shrink towards the eye's axis: same screen position, nearer depth */
    return pg_v3(view_eye.x + (x - view_eye.x) * k, view_eye.y + (y - view_eye.y) * k, z);
}

float depth_k(float z) { return (view_eye.z - z) / view_eye.z; }

/* ----------------------------------------------------------------- textures */
PG_Tex tex_finish(PG_Image *im, bool ps1)
{
    if (ps1) pg_img_ps1(im);
    PG_Tex t = pg3d_texture(im);
    pg_img_free(im);
    return t;
}

void img_blit(PG_Image *dst, const PG_Image *src, int ox, int oy)
{
    for (int j = 0; j < src->h; j++)
        for (int i = 0; i < src->w; i++)
            if (ox + i >= 0 && ox + i < dst->w && oy + j >= 0 && oy + j < dst->h) dst->px[(oy + j) * dst->w + ox + i] = src->px[j * src->w + i];
}

PG_Tex tex_sub(PG_Tex s, int sw, int sh, int x, int y, int w, int h)
{
    float du = (s.u1 - s.u0) / (float)sw, dv = (s.v1 - s.v0) / (float)sh;
    const float inset = 0.06f;                        /* keep nearest sampling inside the rectangle */
    PG_Tex t;
    t.u0 = s.u0 + ((float)x + inset) * du;
    t.u1 = s.u0 + ((float)(x + w) - inset) * du;
    t.v0 = s.v0 + ((float)y + inset) * dv;
    t.v1 = s.v0 + ((float)(y + h) - inset) * dv;
    return t;
}

/* ------------------------------------------------------------------- meshes */
MeshB mb_begin(int max_verts, int max_idx)
{
    MeshB b;
    b.m = pg_mesh_new(max_verts, max_idx);
    b.nv = b.ni = 0;
    b.cap_v = max_verts; b.cap_i = max_idx;
    return b;
}

int mb_vert(MeshB *b, PG_V3 p, PG_V3 n, float u, float v, PG_Color c)
{
    if (b->nv >= b->cap_v) pg_fatal("draw-roll: mesh vertex overflow");
    PG_Vertex *o = &b->m->v[b->nv];
    o->x = p.x; o->y = p.y; o->z = p.z;
    o->nx = n.x; o->ny = n.y; o->nz = n.z;
    o->u = u; o->v = v; o->c = c;
    return b->nv++;
}

void mb_tri(MeshB *b, int i0, int i1, int i2)
{
    const PG_Vertex *a = &b->m->v[i0], *c = &b->m->v[i1], *d = &b->m->v[i2];
    PG_V3 e1 = pg_v3(c->x - a->x, c->y - a->y, c->z - a->z), e2 = pg_v3(d->x - a->x, d->y - a->y, d->z - a->z);
    PG_V3 g = pg_v3_cross(e1, e2);
    if (pg_v3_dot(g, g) < 1e-14f) return;
    PG_V3 n = pg_v3(a->nx + c->nx + d->nx, a->ny + c->ny + d->ny, a->nz + c->nz + d->nz);
    if (pg_v3_dot(g, n) < 0) { int t = i1; i1 = i2; i2 = t; }
    if (b->ni + 3 > b->cap_i) pg_fatal("draw-roll: mesh index overflow");
    b->m->idx[b->ni++] = (uint16_t)i0; b->m->idx[b->ni++] = (uint16_t)i1; b->m->idx[b->ni++] = (uint16_t)i2;
}

void mb_quad(MeshB *b, int a, int c, int d, int e) { mb_tri(b, a, c, d); mb_tri(b, a, d, e); }

PG_Mesh *mb_end(MeshB *b)
{
    b->m->nv = b->nv;
    b->m->ni = b->ni;
    return b->m;
}

/* ------------------------------------------------------------------- sound */
bool snd_ready(int kind, int min_ticks)
{
    static uint64_t last[16];
    uint64_t now = pg_ticks();
    if (kind < 0 || kind > 15) return true;
    if (last[kind] != 0 && now - last[kind] < (uint64_t)min_ticks) return false;
    last[kind] = now ? now : 1;
    return true;
}

void snd_note(int step, float vol, float ms)
{
    if (!snd_ready(0, 3)) return;
    float f = pg_note_hz(step);
    pg_tone(PG_WAVE_BELL, f, f, ms, clampf(vol, 0.0f, 0.8f));
}

void snd_blip(int step, float vol)
{
    if (!snd_ready(1, 5)) return;
    float f = pg_note_hz(step);
    pg_tone(PG_WAVE_SINE, f, f * 1.015f, 110.0f, clampf(vol, 0.0f, 0.5f));
}
