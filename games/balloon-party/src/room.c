/* room.c - the party room, all drawn with pg3d: patterned wall, two sunny windows, bunting, a wooden floor
 * with a round rug, wrapped presents with bows, a party hat, warm sunbeams and floating dust.
 * Everything static is built once as meshes; only the bunting flags sway and the dust drifts.
 * Wall, windows, floor etc. go in the BG layer (drawn first, in the order submitted). */
#include "bp.h"

#include <stdlib.h>
#include <string.h>

#define N_PRESENTS 4
#define N_SWAGS    2
#define N_FLAGS    8
#define N_DUST     18

static PG_Mesh *m_wall, *m_wain, *m_trim, *m_trim_under, *m_floor, *m_rug, *m_glass, *m_rainbow;
static PG_Mesh *m_bar_v, *m_bar_h, *m_mull_v, *m_mull_h, *m_sill, *m_rail, *m_base, *m_twine, *m_hat;
static PG_Mesh *m_gift[N_PRESENTS];
static PG_Tex t_wall, t_trim, t_wain, t_floor, t_rug, t_win[2], t_frame, t_gift[N_PRESENTS], t_bow[N_PRESENTS], t_flags, t_twine, t_rainbow, t_hat;

typedef struct { float x, y, z, vx, vy, ph; } Dust;
static Dust dust[N_DUST];
static float now_t;

typedef struct { float x, y, ang, ph; int pat; } Flag;
static Flag flags[N_SWAGS * N_FLAGS];

/* centre x, z, half extents, bevel, yaw, bow? */
static const struct { float x, z, hx, hy, hz, bev, yaw; bool bow; } GIFT[N_PRESENTS] = {
    { -392.0f, -150.0f, 78.0f, 62.0f, 66.0f, 12.0f,  0.10f, true },
    { -394.0f, -150.0f, 44.0f, 38.0f, 44.0f,  8.0f, -0.45f, true },
    {  404.0f, -120.0f, 70.0f, 52.0f, 60.0f, 11.0f, -0.18f, true },
    {  292.0f,  -44.0f, 40.0f, 36.0f, 40.0f,  8.0f,  0.38f, false },
};

static PG_Color grey(float k) { uint8_t g = (uint8_t)(clampf(k, 0, 1) * 255.0f); return pg_rgb(g, g, g); }

/* ------------------------------------------------------------- mesh helpers */
/* wall shading baked into vertex colours: darker towards the corners and the ceiling */
static float wall_shade(float x, float y)
{
    float side = fabsf(x) / 880.0f, top = clampf((y - 180.0f) / 360.0f, 0, 1), low = clampf((-y - 60.0f) / 160.0f, 0, 1);
    return 1.0f - 0.16f * side * side - 0.13f * top - 0.06f * low;
}

/* a wall-like grid in the XY plane facing +z; every cell repeats the texture once */
static PG_Mesh *grid_xy(float x0, float x1, float y0, float y1, float z, int nx, int ny, bool shade)
{
    Build b = build_new(nx * ny * 4, nx * ny * 6);
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++) {
            float xa = lerpf(x0, x1, (float)i / (float)nx), xb = lerpf(x0, x1, (float)(i + 1) / (float)nx);
            float ya = lerpf(y0, y1, (float)j / (float)ny), yb = lerpf(y0, y1, (float)(j + 1) / (float)ny);
            int a = build_vert(&b, xa, ya, z, 0, 0, 1, 0, 1, shade ? grey(wall_shade(xa, ya)) : pg_rgb(255, 255, 255));
            int c = build_vert(&b, xb, ya, z, 0, 0, 1, 1, 1, shade ? grey(wall_shade(xb, ya)) : pg_rgb(255, 255, 255));
            int d = build_vert(&b, xb, yb, z, 0, 0, 1, 1, 0, shade ? grey(wall_shade(xb, yb)) : pg_rgb(255, 255, 255));
            int e = build_vert(&b, xa, yb, z, 0, 0, 1, 0, 0, shade ? grey(wall_shade(xa, yb)) : pg_rgb(255, 255, 255));
            build_quad(&b, a, c, d, e);
        }
    return build_done(&b);
}

/* the floor: XZ plane facing +y, one texture repeat per cell, slightly darker towards the far wall */
static PG_Mesh *grid_xz(float x0, float x1, float z0, float z1, float y, int nx, int nz)
{
    Build b = build_new(nx * nz * 4, nx * nz * 6);
    for (int j = 0; j < nz; j++)
        for (int i = 0; i < nx; i++) {
            float xa = lerpf(x0, x1, (float)i / (float)nx), xb = lerpf(x0, x1, (float)(i + 1) / (float)nx);
            float za = lerpf(z0, z1, (float)j / (float)nz), zb = lerpf(z0, z1, (float)(j + 1) / (float)nz);
            float sa = 0.74f + 0.26f * smooth01((za - z0) / 300.0f), sb = 0.74f + 0.26f * smooth01((zb - z0) / 300.0f);
            int a = build_vert(&b, xa, y, za, 0, 1, 0, 0, 0, grey(sa));
            int c = build_vert(&b, xb, y, za, 0, 1, 0, 1, 0, grey(sa));
            int d = build_vert(&b, xb, y, zb, 0, 1, 0, 1, 1, grey(sb));
            int e = build_vert(&b, xa, y, zb, 0, 1, 0, 0, 1, grey(sb));
            build_quad(&b, a, e, d, c);                              /* counter-clockwise seen from above */
        }
    return build_done(&b);
}

static PG_Mesh *rug_mesh(float cx, float cz, float rx, float rz, float y, int segs)
{
    Build b = build_new(segs + 2, segs * 3);
    int c0 = build_vert(&b, cx, y, cz, 0, 1, 0, 0.5f, 0.5f, pg_rgb(255, 255, 255));
    for (int i = 0; i <= segs; i++) {
        float a = PG_TAU * (float)i / (float)segs, cs = cosf(a), sn = sinf(a);
        build_vert(&b, cx + cs * rx, y, cz + sn * rz, 0, 1, 0, 0.5f + 0.5f * cs, 0.5f + 0.5f * sn, pg_rgb(255, 255, 255));
    }
    for (int i = 0; i < segs; i++) build_tri(&b, c0, c0 + 2 + i, c0 + 1 + i);
    return build_done(&b);
}

/* a plain horizontal strip facing down (the underside of the crown moulding) */
static PG_Mesh *strip_under(float x0, float x1, float y, float z0, float z1)
{
    Build b = build_new(4, 6);
    PG_Color w = pg_rgb(255, 255, 255);
    int a = build_vert(&b, x0, y, z0, 0, -1, 0, 0, 0, w), c = build_vert(&b, x1, y, z0, 0, -1, 0, 1, 0, w);
    int d = build_vert(&b, x1, y, z1, 0, -1, 0, 1, 1, w), e = build_vert(&b, x0, y, z1, 0, -1, 0, 0, 1, w);
    build_quad(&b, a, c, d, e);
    return build_done(&b);
}

static float swag_y(float s, float sag) { return 372.0f - 4.0f * sag * s * (1.0f - s); }

/* ------------------------------------------------------------------ init */
void room_init(void)
{
    t_wall = tex_upload(paint_wall(), true);       t_trim = tex_upload(paint_trim(), true);
    t_wain = tex_upload(paint_wainscot(), true);   t_floor = tex_upload(paint_floor(), true);
    t_rug = tex_upload(paint_rug(), true);         t_win[0] = tex_upload(paint_window(0), true);
    t_win[1] = tex_upload(paint_window(1), true);  t_frame = tex_upload(paint_frame(), true);
    t_flags = tex_upload(paint_flags(), true);     t_twine = tex_upload(paint_twine(), true);
    t_rainbow = tex_upload(paint_rainbow(), true);
    t_hat = tex_upload(paint_streamer(0xFF7EB6, 0xFFE9F3), true);
    static const uint32_t paper[N_PRESENTS] = { 0x4B8BFF, 0xFF5252, 0x39C46A, 0xA66BFF };
    static const uint32_t ribn[N_PRESENTS] = { 0xFFD23F, 0xFFFFFF, 0xFF7EB8, 0xFFD23F };
    for (int k = 0; k < N_PRESENTS; k++) {
        t_gift[k] = tex_upload(paint_gift(paper[k], ribn[k], k % 3), true);
        t_bow[k] = tex_upload(paint_bow(ribn[k] == 0xFFFFFF ? 0xFFF3F3 : ribn[k]), false);
        m_gift[k] = pg_mesh_bevel_box(GIFT[k].hx, GIFT[k].hy, GIFT[k].hz, GIFT[k].bev);
    }

    m_wall = grid_xy(-864.0f, 864.0f, FLOOR_Y, FLOOR_Y + 768.0f, WALL_Z, 9, 4, true);
    m_wain = grid_xy(-896.0f, 896.0f, FLOOR_Y, -60.0f, WALL_Z + 3.0f, 14, 1, true);
    m_trim = grid_xy(-896.0f, 896.0f, 360.0f, 456.0f, WALL_Z + 30.0f, 14, 1, false);
    m_trim_under = strip_under(-896.0f, 896.0f, 360.0f, WALL_Z + 30.0f, WALL_Z);
    m_floor = grid_xz(-1000.0f, 1000.0f, WALL_Z, 470.0f, FLOOR_Y, 8, 4);
    m_rug = rug_mesh(0, -140.0f, 380.0f, 205.0f, FLOOR_Y + 1.2f, 28);
    m_glass = pg_mesh_quad(150.0f, 200.0f);
    m_rainbow = pg_mesh_quad(120.0f, 60.0f);
    m_bar_v = pg_mesh_box(14.0f, 214.0f, 14.0f);
    m_bar_h = pg_mesh_box(178.0f, 14.0f, 14.0f);
    m_mull_v = pg_mesh_box(5.0f, 200.0f, 9.0f);
    m_mull_h = pg_mesh_box(150.0f, 5.0f, 9.0f);
    m_sill = pg_mesh_box(196.0f, 9.0f, 34.0f);
    m_rail = pg_mesh_bevel_box(900.0f, 11.0f, 10.0f, 4.0f);
    m_base = pg_mesh_box(900.0f, 17.0f, 8.0f);
    m_hat = pg_mesh_cone(27.0f, 40.0f, 10);

    /* bunting: a twine swag per span, flags hang off it */
    const float xs[N_SWAGS + 1] = { -800.0f, 0.0f, 800.0f };
    const float sag = 105.0f;
    const int segs = 22;
    Build b = build_new(N_SWAGS * (segs + 1) * 2, N_SWAGS * segs * 6);
    int fi = 0;
    for (int s = 0; s < N_SWAGS; s++) {
        float xa = xs[s], xb = xs[s + 1];
        int first = b.nv;
        for (int i = 0; i <= segs; i++) {
            float u = (float)i / (float)segs, x = lerpf(xa, xb, u), y = swag_y(u, sag);
            float dydx = -4.0f * sag * (1.0f - 2.0f * u) / (xb - xa), l = sqrtf(1.0f + dydx * dydx);
            float nx = -dydx / l * 2.2f, ny = 1.0f / l * 2.2f;
            build_vert(&b, x + nx, y + ny, WALL_Z + 34.0f, 0, 0, 1, 0, u * 14.0f, pg_rgb(255, 255, 255));
            build_vert(&b, x - nx, y - ny, WALL_Z + 34.0f, 0, 0, 1, 1, u * 14.0f, pg_rgb(255, 255, 255));
        }
        for (int i = 0; i < segs; i++) {
            int a = first + i * 2;
            build_tri(&b, a, a + 1, a + 3);
            build_tri(&b, a, a + 3, a + 2);
        }
        for (int k = 0; k < N_FLAGS; k++) {
            float u = ((float)k + 0.5f) / (float)N_FLAGS;
            Flag *f = &flags[fi++];
            f->x = lerpf(xa, xb, u); f->y = swag_y(u, sag) - 2.0f;
            f->ang = atanf(-4.0f * sag * (1.0f - 2.0f * u) / (xb - xa));
            f->ph = (float)(fi * 7) * 0.9f; f->pat = (fi * 3 + s) & 7;
        }
    }
    m_twine = build_done(&b);

    for (int i = 0; i < N_DUST; i++)
        dust[i] = (Dust){ frand(-560, 560), frand(-240, 380), frand(-260, 120), frand(-6, 6), frand(2, 9), frand(0, PG_TAU) };
    pg3d_sky(pg_hex(0xB9E6FF), pg_hex(0xFFF1D6));
    pg3d_light(pg_v3(LIGHT_X, LIGHT_Y, LIGHT_Z), pg_rgb(118, 112, 100), pg_rgb(166, 172, 190), pg_rgb(138, 124, 116));
}

void room_free(void)
{
    PG_Mesh *all[] = { m_wall, m_wain, m_trim, m_trim_under, m_floor, m_rug, m_glass, m_rainbow, m_bar_v, m_bar_h, m_mull_v, m_mull_h,
                       m_sill, m_rail, m_base, m_twine, m_hat, m_gift[0], m_gift[1], m_gift[2], m_gift[3] };
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) pg_mesh_free(all[i]);
}

void room_update(float t)
{
    now_t = t;
    for (int i = 0; i < N_DUST; i++) {
        Dust *d = &dust[i];
        d->x += (d->vx + sinf(t * 0.4f + d->ph) * 5.0f) * PG_DT;
        d->y += (d->vy + sinf(t * 0.7f + d->ph * 1.7f) * 4.0f) * PG_DT;
        if (d->y > 400) d->y = -240;
        if (d->x > 600) d->x = -600;
        if (d->x < -600) d->x = 600;
    }
}

/* ------------------------------------------------------------------ draw */
static void flag_tri(const Flag *f, float t)
{
    const float hw = 38.0f, hh = 98.0f;
    float sw = f->ang + sinf(t * 1.25f + f->ph) * 0.075f, c = cosf(sw), s = sinf(sw);
    float u0 = (float)(f->pat & 3) * 0.25f, v0 = (float)(f->pat >> 2) * 0.5f;
    PG_Vertex a, b, d;
    memset(&a, 0, sizeof a);
    a.nz = 1.0f; a.c = pg_rgb(255, 255, 255);
    b = a; d = a;
    a.x = f->x - c * hw;  a.y = f->y - s * hw;  a.z = WALL_Z + 36.0f; a.u = u0;          a.v = v0;
    b.x = f->x + c * hw;  b.y = f->y + s * hw;  b.z = WALL_Z + 36.0f; b.u = u0 + 0.25f;  b.v = v0;
    d.x = f->x + s * hh;  d.y = f->y - c * hh;  d.z = WALL_Z + 36.0f; d.u = u0 + 0.125f; d.v = v0 + 0.5f;
    pg3d_tri(t_flags, &a, &b, &d, PG3D_BG | PG3D_TWOSIDED);
}

void room_draw_wall(void)
{
    PG_M4 I = pg_m4_identity(), m;
    PG_Color white = pg_rgb(255, 255, 255);
    pg3d_mesh(m_wall, t_wall, &I, white, PG3D_BG);
    m = pg_m4_translate(0, 28.0f, WALL_Z + 2.0f);
    pg3d_mesh(m_rainbow, t_rainbow, &m, white, PG3D_BG);

    for (int w = 0; w < 2; w++) {
        float cx = w ? 312.0f : -312.0f, cy = 150.0f, z = WALL_Z;
        m = pg_m4_translate(cx, cy, z + 5.0f);
        pg3d_mesh(m_glass, t_win[w], &m, white, PG3D_BG | PG3D_UNLIT);
        m = pg_m4_translate(cx - 164.0f, cy, z + 10.0f); pg3d_mesh(m_bar_v, t_frame, &m, white, PG3D_BG);
        m = pg_m4_translate(cx + 164.0f, cy, z + 10.0f); pg3d_mesh(m_bar_v, t_frame, &m, white, PG3D_BG);
        m = pg_m4_translate(cx, cy + 214.0f, z + 10.0f); pg3d_mesh(m_bar_h, t_frame, &m, white, PG3D_BG);
        m = pg_m4_translate(cx, cy - 214.0f, z + 10.0f); pg3d_mesh(m_bar_h, t_frame, &m, white, PG3D_BG);
        m = pg_m4_translate(cx, cy, z + 9.0f);           pg3d_mesh(m_mull_v, t_frame, &m, white, PG3D_BG);
        m = pg_m4_translate(cx, cy + 30.0f, z + 9.0f);   pg3d_mesh(m_mull_h, t_frame, &m, white, PG3D_BG);
        m = pg_m4_translate(cx, cy - 232.0f, z + 28.0f); pg3d_mesh(m_sill, t_frame, &m, white, PG3D_BG);
    }
    pg3d_mesh(m_trim, t_trim, &I, white, PG3D_BG);
    pg3d_mesh(m_trim_under, t_frame, &I, white, PG3D_BG);
    pg3d_mesh(m_wain, t_wain, &I, white, PG3D_BG);
    m = pg_m4_translate(0, -60.0f, WALL_Z + 10.0f);          pg3d_mesh(m_rail, t_frame, &m, white, PG3D_BG);
    m = pg_m4_translate(0, FLOOR_Y + 16.0f, WALL_Z + 8.0f);  pg3d_mesh(m_base, t_frame, &m, white, PG3D_BG);
}

void room_draw_floor(void)
{
    PG_M4 I = pg_m4_identity(), m;
    PG_Color white = pg_rgb(255, 255, 255);
    pg3d_mesh(m_twine, t_twine, &I, white, PG3D_BG | PG3D_TWOSIDED | PG3D_UNLIT);
    for (int i = 0; i < N_SWAGS * N_FLAGS; i++) flag_tri(&flags[i], now_t);

    pg3d_mesh(m_floor, t_floor, &I, white, PG3D_BG);
    pg3d_mesh(m_rug, t_rug, &I, white, PG3D_BG);
    for (int k = 0; k < N_PRESENTS; k++) {
        float cy = FLOOR_Y + GIFT[k].hy;
        if (k == 1) cy = FLOOR_Y + 2.0f * GIFT[0].hy + GIFT[1].hy;
        if (k != 1) pg3d_blob_shadow(GIFT[k].x + 8.0f, FLOOR_Y + 2.0f, GIFT[k].z + 6.0f, GIFT[k].hx * 1.7f, 0.42f);
        m = pg_m4_mul(pg_m4_translate(GIFT[k].x, cy, GIFT[k].z), pg_m4_rot_y(GIFT[k].yaw));
        pg3d_mesh(m_gift[k], t_gift[k], &m, white, PG3D_BG);
        if (GIFT[k].bow) {
            PG_V3 bp = pg_v3(GIFT[k].x, cy + GIFT[k].hy + 12.0f, GIFT[k].z + GIFT[k].hz * 0.45f);
            pg3d_sprite(t_bow[k], bp, 72.0f, 72.0f, 0.0f, white, PG3D_BG);
        }
    }
    /* a party hat on the small purple present, with a pompom */
    float hx = GIFT[3].x, hz = GIFT[3].z, hb = FLOOR_Y + 2.0f * GIFT[3].hy;
    m = pg_m4_mul(pg_m4_translate(hx, hb + 38.0f, hz), pg_m4_rot_z(-0.2f));
    pg3d_mesh(m_hat, t_hat, &m, white, PG3D_BG);
    pg3d_sprite(pg3d_builtin(PG3D_TEX_DISC), pg_v3(hx + 0.2f * 40.0f, hb + 38.0f + 40.0f * 0.98f, hz + 4.0f), 17.0f, 17.0f, 0, pg_rgb(255, 226, 80), PG3D_BG);
}

/* additive extras: two soft sunbeams from the left window, and dust catching the light */
void room_draw_ambient(void)
{
    float t = now_t, pulse = 0.82f + 0.18f * sinf(t * 0.7f);
    static const float beam[2][6] = { { -432.0f, -300.0f, 235.0f, 370.0f, -440.0f, 0.17f }, { -250.0f, -196.0f, 110.0f, 310.0f, -330.0f, 0.12f } };
    const unsigned fl = PG3D_ADDITIVE | PG3D_UNLIT | PG3D_TWOSIDED | PG3D_NOFOG;
    for (int k = 0; k < 2; k++) {
        float x0 = beam[k][0], x1 = beam[k][1], y0 = beam[k][2], dx = beam[k][3], dy = beam[k][4], a = beam[k][5] * pulse;
        /* 3 columns x 3 rows: transparent edges and ends, brightest in the middle near the window */
        PG_Vertex v[3][3];
        memset(v, 0, sizeof v);
        const float zs = WALL_Z + 14.0f, ze = WALL_Z + 150.0f;
        static const float rowt[3] = { 0.0f, 0.12f, 1.0f }, rowk[3] = { 0.0f, 1.0f, 0.0f }, cola[3] = { 0.0f, 1.0f, 0.0f };
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++) {
                float u = (float)c * 0.5f, tt = rowt[r];
                float al = cola[c] * rowk[r] * a;
                v[r][c].x = lerpf(x0, x1, u) + dx * tt; v[r][c].y = y0 + dy * tt; v[r][c].z = lerpf(zs, ze, tt); v[r][c].nz = 1.0f;
                v[r][c].c = pg_rgba(255, 240, 190, (uint8_t)(al * 255.0f));
            }
        for (int r = 0; r < 2; r++)
            for (int c = 0; c < 2; c++) {
                pg3d_tri(pg3d_white(), &v[r][c], &v[r][c + 1], &v[r + 1][c + 1], fl);
                pg3d_tri(pg3d_white(), &v[r][c], &v[r + 1][c + 1], &v[r + 1][c], fl);
            }
    }
    for (int i = 0; i < N_DUST; i++) {
        const Dust *d = &dust[i];
        float tw = 0.55f + 0.45f * sinf(t * 1.1f + d->ph * 3.0f);
        pg3d_sprite(pg3d_builtin(PG3D_TEX_GLOW), pg_v3(d->x, d->y, d->z), 12.0f, 12.0f, 0, pg_rgba(255, 246, 210, (uint8_t)(150.0f * tw)), PG3D_ADDITIVE);
    }
}
