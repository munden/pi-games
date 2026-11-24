/* build.c - the few meshes the SDK generators cannot make: tiled walls, a rainbow arch, a bunting flag. */
#include "pit.h"

#include <math.h>

static void set_vert(PG_Vertex *o, PG_V3 p, PG_V3 n, float u, float v)
{
    o->x = p.x; o->y = p.y; o->z = p.z;
    o->nx = n.x; o->ny = n.y; o->nz = n.z;
    o->u = u; o->v = v;
    o->c = pg_rgb(255, 255, 255);
}

/* triangle (i0,i1,i2), flipped if its winding disagrees with `want` (the outward normal): the SDK culls back faces */
static void add_tri(PG_Mesh *m, int *ni, int i0, int i1, int i2, PG_V3 want)
{
    const PG_Vertex *a = &m->v[i0], *b = &m->v[i1], *c = &m->v[i2];
    PG_V3 e1 = pg_v3(b->x - a->x, b->y - a->y, b->z - a->z), e2 = pg_v3(c->x - a->x, c->y - a->y, c->z - a->z);
    if (pg_v3_dot(pg_v3_cross(e1, e2), want) < 0) { int t = i1; i1 = i2; i2 = t; }
    m->idx[(*ni)++] = (uint16_t)i0; m->idx[(*ni)++] = (uint16_t)i1; m->idx[(*ni)++] = (uint16_t)i2;
}

static PG_V3 vert_normals(const PG_Mesh *m, int i0, int i1, int i2)
{
    const PG_Vertex *a = &m->v[i0], *b = &m->v[i1], *c = &m->v[i2];
    return pg_v3(a->nx + b->nx + c->nx, a->ny + b->ny + c->ny, a->nz + b->nz + c->nz);
}

PG_Mesh *mesh_tiles(PG_V3 origin, PG_V3 du, PG_V3 dv, int nx, int ny, PG_V3 normal, bool flip_v)
{
    PG_Mesh *m = pg_mesh_new(nx * ny * 4, nx * ny * 6);
    int nv = 0, ni = 0;
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++) {
            PG_V3 p00 = pg_v3_add(origin, pg_v3_add(pg_v3_scale(du, (float)i), pg_v3_scale(dv, (float)j)));
            PG_V3 p10 = pg_v3_add(p00, du), p01 = pg_v3_add(p00, dv), p11 = pg_v3_add(p10, dv);
            int base = nv;
            set_vert(&m->v[nv++], p00, normal, 0, flip_v ? 1.0f : 0.0f);
            set_vert(&m->v[nv++], p10, normal, 1, flip_v ? 1.0f : 0.0f);
            set_vert(&m->v[nv++], p11, normal, 1, flip_v ? 0.0f : 1.0f);
            set_vert(&m->v[nv++], p01, normal, 0, flip_v ? 0.0f : 1.0f);
            add_tri(m, &ni, base, base + 1, base + 2, normal);
            add_tri(m, &ni, base, base + 2, base + 3, normal);
        }
    m->nv = nv; m->ni = ni;
    return m;
}

PG_Mesh *mesh_arch(float radius, float tube, int segs, int sides)
{
    PG_Mesh *m = pg_mesh_new((segs + 1) * (sides + 1), segs * sides * 6);
    int nv = 0, ni = 0;
    for (int i = 0; i <= segs; i++) {
        float a = PG_PI * (float)i / (float)segs, ca = cosf(a), sa = sinf(a);
        for (int j = 0; j <= sides; j++) {
            float t = PG_PI * (float)j / (float)sides, ct = cosf(t), st = sinf(t);     /* t: 0 outer edge, pi inner edge */
            PG_V3 n = pg_v3(ct * ca, ct * sa, st);
            set_vert(&m->v[nv++], pg_v3((radius + tube * ct) * ca, (radius + tube * ct) * sa, tube * st), n,
                     (float)i / (float)segs, (float)j / (float)sides);
        }
    }
    for (int i = 0; i < segs; i++)
        for (int j = 0; j < sides; j++) {
            int a = i * (sides + 1) + j, b = a + 1, c = a + sides + 1, d = c + 1;
            add_tri(m, &ni, a, c, d, vert_normals(m, a, c, d));
            add_tri(m, &ni, a, d, b, vert_normals(m, a, d, b));
        }
    m->nv = nv; m->ni = ni;
    return m;
}

PG_Mesh *mesh_pennant(float width, float height)
{
    PG_Mesh *m = pg_mesh_new(3, 3);
    PG_V3 n = pg_v3(0, 0, 1);
    set_vert(&m->v[0], pg_v3(-width * 0.5f, 0, 0), n, 0, 0);
    set_vert(&m->v[1], pg_v3(width * 0.5f, 0, 0), n, 1, 0);
    set_vert(&m->v[2], pg_v3(0, -height, 0), n, 0.5f, 1);
    int ni = 0;
    add_tri(m, &ni, 0, 1, 2, n);
    return m;
}
