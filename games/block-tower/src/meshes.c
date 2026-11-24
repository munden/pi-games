/* meshes.c - every mesh Block Tower draws, built once in init (no per-frame allocation). */
#include "bt.h"

#include <math.h>
#include <string.h>

static PG_Vertex vtx(float x, float y, float z, float nx, float ny, float nz, float u, float v, PG_Color c)
{
    PG_Vertex a = { x, y, z, nx, ny, nz, u, v, c };
    return a;
}

/* flat nx*ny grid in the XY plane facing +z, uv (0,0) top-left .. (1,1) bottom-right; subdivided so the
 * affine texture mapping stays straight on walls seen at an angle */
static PG_Mesh *mesh_grid(float hw, float hh, int nx, int ny)
{
    PG_Mesh *m = pg_mesh_new((nx + 1) * (ny + 1), nx * ny * 6);
    int k = 0;
    for (int j = 0; j <= ny; j++)
        for (int i = 0; i <= nx; i++) {
            float fu = (float)i / (float)nx, fv = (float)j / (float)ny;
            m->v[k++] = vtx(-hw + 2 * hw * fu, hh - 2 * hh * fv, 0, 0, 0, 1, fu, fv, pg_rgb(255, 255, 255));
        }
    int q = 0;
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++) {
            int a = j * (nx + 1) + i, b = a + 1, c = a + nx + 1, d = c + 1;
            m->idx[q++] = (uint16_t)a; m->idx[q++] = (uint16_t)c; m->idx[q++] = (uint16_t)d;
            m->idx[q++] = (uint16_t)a; m->idx[q++] = (uint16_t)d; m->idx[q++] = (uint16_t)b;
        }
    return m;
}

/* round disc facing +z (button face); uv is the inscribed circle of the texture */
static PG_Mesh *mesh_disc_xy(float r, int segs)
{
    PG_Mesh *m = pg_mesh_new(segs + 2, segs * 3);
    PG_Color w = pg_rgb(255, 255, 255);
    m->v[0] = vtx(0, 0, 0, 0, 0, 1, 0.5f, 0.5f, w);
    for (int i = 0; i <= segs; i++) {
        float a = PG_TAU * (float)i / (float)segs;
        m->v[i + 1] = vtx(r * cosf(a), r * sinf(a), 0, 0, 0, 1, 0.5f + 0.5f * cosf(a), 0.5f - 0.5f * sinf(a), w);
    }
    for (int i = 0; i < segs; i++) { m->idx[i * 3] = 0; m->idx[i * 3 + 1] = (uint16_t)(i + 1); m->idx[i * 3 + 2] = (uint16_t)(i + 2); }
    return m;
}

/* round disc lying on the floor facing +y (rug); texture's up is -z */
static PG_Mesh *mesh_disc_xz(float r, int segs)
{
    PG_Mesh *m = pg_mesh_new(segs + 2, segs * 3);
    PG_Color w = pg_rgb(255, 255, 255);
    m->v[0] = vtx(0, 0, 0, 0, 1, 0, 0.5f, 0.5f, w);
    for (int i = 0; i <= segs; i++) {
        float a = PG_TAU * (float)i / (float)segs;
        m->v[i + 1] = vtx(r * cosf(a), 0, r * sinf(a), 0, 1, 0, 0.5f + 0.5f * cosf(a), 0.5f + 0.5f * sinf(a), w);
    }
    for (int i = 0; i < segs; i++) { m->idx[i * 3] = 0; m->idx[i * 3 + 1] = (uint16_t)(i + 2); m->idx[i * 3 + 2] = (uint16_t)(i + 1); }
    return m;
}

/* hanging curtain: vertex colours carry the folds */
static PG_Mesh *mesh_curtain(float w, float h, int folds)
{
    int nx = folds, ny = 3;
    PG_Mesh *m = pg_mesh_new((nx + 1) * (ny + 1), nx * ny * 6);
    int k = 0;
    for (int j = 0; j <= ny; j++)
        for (int i = 0; i <= nx; i++) {
            float fu = (float)i / (float)nx, fv = (float)j / (float)ny;
            float f = (i & 1) ? 0.78f : 1.0f;
            f *= 1.0f - 0.18f * fv;                           /* darker toward the hem */
            uint8_t c = (uint8_t)(255.0f * f);
            float sway = (i & 1) ? 3.0f : 0.0f;               /* folds stand off the wall a little */
            m->v[k++] = vtx(-w * 0.5f + w * fu, h * 0.5f - h * fv, sway, 0, 0, 1, fu, fv, pg_rgb(c, c, c));
        }
    int q = 0;
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++) {
            int a = j * (nx + 1) + i, b = a + 1, c = a + nx + 1, d = c + 1;
            m->idx[q++] = (uint16_t)a; m->idx[q++] = (uint16_t)c; m->idx[q++] = (uint16_t)d;
            m->idx[q++] = (uint16_t)a; m->idx[q++] = (uint16_t)d; m->idx[q++] = (uint16_t)b;
        }
    return m;
}

/* bake a rotation about the x axis into a mesh (positions and normals) */
static void mesh_rot_x(PG_Mesh *m, float a)
{
    float c = cosf(a), s = sinf(a);
    for (int i = 0; i < m->nv; i++) {
        PG_Vertex *v = &m->v[i];
        float y = v->y * c - v->z * s, z = v->y * s + v->z * c;
        float ny = v->ny * c - v->nz * s, nz = v->ny * s + v->nz * c;
        v->y = y; v->z = z; v->ny = ny; v->nz = nz;
    }
}

/* every mesh is registered so meshes_free() needs no knowledge of the struct layout */
static PG_Mesh *reg(Meshes *m, PG_Mesh *mesh)
{
    if (m->n < (int)(sizeof m->all / sizeof m->all[0])) m->all[m->n++] = mesh;
    else pg_mesh_free(mesh);
    return mesh;
}

void meshes_build(Game *g)
{
    Meshes *m = &g->mesh;
    m->n = 0;
    for (int t = 0; t < T_TYPES; t++) {
        const ShapeDef *s = bt_shape(t);
        m->shape[t] = reg(m, pg_mesh_bevel_box(s->hx, s->hy, s->hz, s->bevel));
    }
    m->ball = reg(m, pg_mesh_sphere(1.0f, 14, 10));
    m->quad = reg(m, pg_mesh_quad(1.0f, 1.0f));
    m->wall_tile = reg(m, mesh_grid(80, 80, 4, 1));       /* side walls: subdivided, they are seen at an angle */
    m->back_tile = reg(m, mesh_grid(80, 80, 1, 1));       /* back wall: parallel to the screen, no subdivision needed */
    m->wain_tile = reg(m, mesh_grid(64, 55, 4, 1));
    m->floor_tile = reg(m, pg_mesh_floor(128, 64, 1, 4));
    m->rug = reg(m, mesh_disc_xz(1.0f, 32));

    m->chest_body = reg(m, pg_mesh_bevel_box(CHEST_HX, 58, CHEST_HZ, 6));
    m->chest_lid = reg(m, pg_mesh_bevel_box(CHEST_HX + 4, 12, CHEST_HZ + 4, 5));
    m->chest_plate = reg(m, pg_mesh_quad(CHEST_HX, 58));
    m->btn_face = reg(m, mesh_disc_xy(BTN_R - 5.0f, 28));
    m->btn_rim = reg(m, pg_mesh_torus(BTN_R - 2.0f, 6.5f, 28, 8));
    mesh_rot_x(m->btn_rim, PG_PI * 0.5f);

    m->baseboard = reg(m, pg_mesh_bevel_box(442, 13, 5, 3));
    m->rail = reg(m, pg_mesh_bevel_box(442, 5, 7, 2.5f));
    m->side_base = reg(m, pg_mesh_bevel_box(5, 13, 215, 3));
    m->side_rail = reg(m, pg_mesh_bevel_box(7, 5, 215, 2.5f));
    m->sill = reg(m, pg_mesh_bevel_box(114, 6, 20, 4));
    m->frame_h = reg(m, pg_mesh_bevel_box(106, 8, 9, 3));
    m->frame_v = reg(m, pg_mesh_bevel_box(8, 95, 9, 3));
    m->bar_h = reg(m, pg_mesh_bevel_box(96, 4, 5, 2));
    m->bar_v = reg(m, pg_mesh_bevel_box(4, 86, 5, 2));
    m->rod = reg(m, pg_mesh_bevel_box(122, 3, 3, 1.2f));
    m->shelf = reg(m, pg_mesh_bevel_box(135, 6, 34, 3));
    m->bracket = reg(m, pg_mesh_bevel_box(5, 17, 26, 2));
    m->curtain = reg(m, mesh_curtain(52, 206, 6));
    m->mini = reg(m, pg_mesh_bevel_box(16, 16, 16, 3));
    m->poster = reg(m, pg_mesh_quad(30, 38));
    m->poster_frame = reg(m, pg_mesh_bevel_box(36, 44, 4, 3));
    m->car_body = reg(m, pg_mesh_bevel_box(40, 13, 23, 4));
    m->car_cab = reg(m, pg_mesh_bevel_box(21, 10, 19, 4));
    m->wheel = reg(m, pg_mesh_cylinder(1.0f, 1.0f, 10));
    mesh_rot_x(m->wheel, PG_PI * 0.5f);

    m->sph = reg(m, pg_mesh_sphere(1.0f, 9, 6));
    m->sph_lo = reg(m, pg_mesh_sphere(1.0f, 6, 4));      /* tiny parts: ears, noses, eyes, lamps */
    m->cone = reg(m, pg_mesh_cone(1.0f, 1.0f, 10));
    m->cyl = reg(m, pg_mesh_cylinder(1.0f, 1.0f, 10));
    m->torus = reg(m, pg_mesh_torus(1.0f, 0.3f, 12, 6));
}

void meshes_free(Game *g)
{
    Meshes *m = &g->mesh;
    for (int i = 0; i < m->n; i++) pg_mesh_free(m->all[i]);
    memset(m, 0, sizeof *m);
}
