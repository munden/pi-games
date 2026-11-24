/* scene.c - the candy playroom (a real 3D box: back wall, side walls, ceiling, foam-mat floor, window, toy shelf,
 * bunting), the camera, and the lit 3D "pillow" that every jelly blob is drawn as, with its face.
 *
 * Everything static is submitted with PG3D_BG in painter's order; blobs, faces and effects are depth sorted. */
#include "scene.h"

#include <math.h>
#include <string.h>

#define ROOM_X     (SB_WALL_X + 4.0f)
#define WALL_Z     (-220.0f)
#define FLOOR_Y    (SB_FLOOR_Y - 1.0f)
#define CEIL_Y     (SB_CEIL_Y + 12.0f)
#define RAIL_Y     (-62.0f)
#define FRONT_Z    260.0f

#define CAM_FOV    44.0f
#define DOME_K     0.52f          /* dome height as a fraction of the blob's equivalent radius */
#define SKIRT_K    0.40f          /* thickness of the side wall behind the rim */
#define RHO        0.60f          /* radius of the inner ring of the pillow */

static const Art *A;
static PG_Mesh *m_wall, *m_wain, *m_swl, *m_swl_w, *m_swr, *m_swr_w, *m_ceil, *m_floor;
static PG_Mesh *m_rail, *m_sill, *m_bar_h, *m_bar_v, *m_frame_h, *m_frame_v, *m_board, *m_brace;
static PG_Mesh *m_block, *m_ball, *m_duck_body, *m_duck_head, *m_beak;
static PG_M4 ident;

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ----------------------------------------------------------------------- mesh building */
/* A grid of nu x nv quads, each showing the whole texture once (tiled wallpaper, foam tiles). */
static PG_Mesh *grid_mesh(PG_V3 o, PG_V3 du, PG_V3 dv, int nu, int nv, PG_V3 n)
{
    PG_Mesh *m = pg_mesh_new(nu * nv * 4, nu * nv * 6);
    static const float U[4] = { 0, 1, 1, 0 }, V[4] = { 1, 1, 0, 0 };
    int vi = 0, ii = 0;
    for (int j = 0; j < nv; j++)
        for (int i = 0; i < nu; i++) {
            PG_V3 p00 = pg_v3_add(o, pg_v3_add(pg_v3_scale(du, (float)i), pg_v3_scale(dv, (float)j)));
            PG_V3 p10 = pg_v3_add(p00, du), p01 = pg_v3_add(p00, dv), p11 = pg_v3_add(p10, dv);
            PG_V3 P[4] = { p00, p10, p11, p01 };
            for (int k = 0; k < 4; k++) {
                PG_Vertex *v = &m->v[vi + k];
                v->x = P[k].x; v->y = P[k].y; v->z = P[k].z; v->nx = n.x; v->ny = n.y; v->nz = n.z;
                v->u = U[k]; v->v = V[k]; v->c = pg_rgb(255, 255, 255);
            }
            bool flip = pg_v3_dot(pg_v3_cross(pg_v3_sub(p10, p00), pg_v3_sub(p11, p00)), n) < 0;
            static const uint16_t ccw[6] = { 0, 1, 2, 0, 2, 3 }, cw[6] = { 0, 2, 1, 0, 3, 2 };
            for (int k = 0; k < 6; k++) m->idx[ii + k] = (uint16_t)(vi + (flip ? cw[k] : ccw[k]));
            vi += 4; ii += 6;
        }
    return m;
}

void scene_init(const Art *art)
{
    A = art;
    ident = pg_m4_identity();
    const float W = 2.0f * ROOM_X, D = FRONT_Z - WALL_Z;
    float hi = CEIL_Y - RAIL_Y, lo = RAIL_Y - FLOOR_Y;
    m_wall  = grid_mesh(pg_v3(-ROOM_X, RAIL_Y, WALL_Z), pg_v3(W / 6, 0, 0), pg_v3(0, hi / 2, 0), 6, 2, pg_v3(0, 0, 1));
    m_wain  = grid_mesh(pg_v3(-ROOM_X, FLOOR_Y, WALL_Z), pg_v3(W / 8, 0, 0), pg_v3(0, lo, 0), 8, 1, pg_v3(0, 0, 1));
    m_swl   = grid_mesh(pg_v3(-ROOM_X, RAIL_Y, WALL_Z), pg_v3(0, 0, 100), pg_v3(0, hi / 2, 0), 3, 2, pg_v3(1, 0, 0));
    m_swl_w = grid_mesh(pg_v3(-ROOM_X, FLOOR_Y, WALL_Z), pg_v3(0, 0, 100), pg_v3(0, lo, 0), 3, 1, pg_v3(1, 0, 0));
    m_swr   = grid_mesh(pg_v3(ROOM_X, RAIL_Y, WALL_Z), pg_v3(0, 0, 100), pg_v3(0, hi / 2, 0), 3, 2, pg_v3(-1, 0, 0));
    m_swr_w = grid_mesh(pg_v3(ROOM_X, FLOOR_Y, WALL_Z), pg_v3(0, 0, 100), pg_v3(0, lo, 0), 3, 1, pg_v3(-1, 0, 0));
    m_ceil  = grid_mesh(pg_v3(-ROOM_X, CEIL_Y, WALL_Z), pg_v3(W / 6, 0, 0), pg_v3(0, 0, 100), 6, 3, pg_v3(0, -1, 0));
    m_floor = grid_mesh(pg_v3(-ROOM_X, FLOOR_Y, WALL_Z), pg_v3(W / 7, 0, 0), pg_v3(0, 0, D / 4), 7, 4, pg_v3(0, 1, 0));
    static const uint32_t mat[4] = { 0xD9F6E8, 0xE6DEFC, 0xFFF3CE, 0xFFE0E8 };      /* pastel foam tiles */
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 7; i++) {
            PG_Color c = pg_hex(mat[(i + j * 2) & 3]);
            for (int k = 0; k < 4; k++) m_floor->v[(j * 7 + i) * 4 + k].c = c;
        }

    m_rail     = pg_mesh_bevel_box(ROOM_X, 6, 6, 3);
    m_sill     = pg_mesh_bevel_box(104, 7, 20, 3);
    m_bar_h    = pg_mesh_box(80, 4, 4);
    m_bar_v    = pg_mesh_box(4, 86, 4);
    m_frame_h  = pg_mesh_bevel_box(94, 9, 8, 3);
    m_frame_v  = pg_mesh_bevel_box(9, 104, 8, 3);
    m_board    = pg_mesh_bevel_box(142, 7, 26, 3);
    m_brace    = pg_mesh_bevel_box(6, 24, 22, 2);
    m_block    = pg_mesh_bevel_box(22, 22, 22, 6);
    m_ball     = pg_mesh_sphere(28, 12, 8);
    m_duck_body = pg_mesh_sphere(24, 12, 8);
    m_duck_head = pg_mesh_sphere(14, 10, 7);
    m_beak     = pg_mesh_cone(5, 6, 6);

    pg3d_sky(pg_hex(0xCFE5FF), pg_hex(0xFFEFD8));
    pg3d_light(pg_v3(-0.40f, 0.70f, 0.60f), pg_hex(0xFFF2DC), pg_hex(0x9AA5C9), pg_hex(0x8F7589));
    pg3d_fog(pg_hex(0xF0E6FF), 720.0f, 1500.0f);
}

void scene_shutdown(void)
{
    PG_Mesh **all[] = { &m_wall, &m_wain, &m_swl, &m_swl_w, &m_swr, &m_swr_w, &m_ceil, &m_floor, &m_rail, &m_sill, &m_bar_h, &m_bar_v,
                        &m_frame_h, &m_frame_v, &m_board, &m_brace, &m_block, &m_ball, &m_duck_body, &m_duck_head, &m_beak };
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) { pg_mesh_free(*all[i]); *all[i] = NULL; }
}

/* ------------------------------------------------------------------------- camera */
void scene_camera(float t, PG_Camera *cam)
{
    *cam = pg3d_camera_2d_fov(CAM_FOV);
    cam->eye.x = sinf(t * 0.31f) * 36.0f;
    cam->eye.y = 44.0f + sinf(t * 0.23f + 1.0f) * 9.0f;
    cam->target = pg_v3(0, -14.0f, 0);
}

bool scene_unproject(float t, float sx, float sy, float *wx, float *wy)
{
    PG_Camera c;
    scene_camera(t, &c);
    PG_V3 fwd = pg_v3_norm(pg_v3_sub(c.target, c.eye));
    PG_V3 right = pg_v3_norm(pg_v3_cross(fwd, c.up));
    PG_V3 up = pg_v3_cross(right, fwd);
    float focal = (PG_H * 0.5f) / tanf(c.fov_deg * 0.5f * PG_PI / 180.0f);
    PG_V3 d = pg_v3_add(fwd, pg_v3_add(pg_v3_scale(right, (sx - PG_W * 0.5f) / focal), pg_v3_scale(up, (PG_H * 0.5f - sy) / focal)));
    if (fabsf(d.z) < 1e-5f) return false;
    float k = (0.0f - c.eye.z) / d.z;
    if (k < 0) return false;
    *wx = c.eye.x + d.x * k; *wy = c.eye.y + d.y * k;
    return true;
}

/* ----------------------------------------------------------------------- the room */
static void tri_flat(PG_Tex tex, PG_V3 a, PG_V3 b, PG_V3 c, PG_Color col, float ua, float va, float ub, float vb, float uc, float vc, unsigned fl)
{
    PG_Vertex p[3] = { { a.x, a.y, a.z, 0, 0, 1, ua, va, col }, { b.x, b.y, b.z, 0, 0, 1, ub, vb, col }, { c.x, c.y, c.z, 0, 0, 1, uc, vc, col } };
    pg3d_tri(tex, &p[0], &p[1], &p[2], fl);
}

static void draw_bunting(float t)
{
    static const uint32_t pal[6] = { 0xFF6F91, 0xFFC93C, 0x7BDC6A, 0x58B8FF, 0xB187FF, 0xFF9A4D };
    const float y0 = CEIL_Y - 12.0f, sag = 42.0f, L = ROOM_X - 4.0f, z = WALL_Z + 6.0f;
    PG_Tex white = pg3d_white();
    const int flags = 15;
    float px = 0, py = 0;
    for (int i = 0; i <= 24; i++) {                                       /* the string */
        float x = -L + 2 * L * (float)i / 24.0f, u = x / L, y = y0 - sag * (1.0f - u * u);
        if (i > 0) {
            PG_Color rc = pg_rgb(255, 250, 244);
            PG_V3 a = pg_v3(px, py + 1.6f, z), b = pg_v3(x, y + 1.6f, z), c = pg_v3(x, y - 1.6f, z), d = pg_v3(px, py - 1.6f, z);
            tri_flat(white, a, d, c, rc, 0, 0, 0, 0, 0, 0, PG3D_BG | PG3D_TWOSIDED);
            tri_flat(white, a, c, b, rc, 0, 0, 0, 0, 0, 0, PG3D_BG | PG3D_TWOSIDED);
        }
        px = x; py = y;
    }
    for (int k = 0; k < flags; k++) {                                     /* the flags */
        float x = -L + 2 * L * ((float)k + 0.5f) / (float)flags, u = x / L;
        float y = y0 - sag * (1.0f - u * u), slope = 2.0f * sag * u / L;
        float hw = 21.0f, h = 48.0f, sway = sinf(t * 1.7f + (float)k * 0.9f) * 5.0f;
        PG_V3 tl = pg_v3(x - hw, y - hw * slope, z + 1), tr = pg_v3(x + hw, y + hw * slope, z + 1), tip = pg_v3(x + sway, y - h, z + 1);
        tri_flat(A->flag, tl, tip, tr, pg_hex(pal[k % 6]), 0, 0, 0.5f, 1, 1, 0, PG3D_BG | PG3D_TWOSIDED);
    }
}

static void draw_window(float t)
{
    const float cx = -250.0f, cy = 98.0f, z = WALL_Z;
    /* sky behind the glass: a textured quad built on the fly from two triangles */
    PG_Tex sky = A->sky;
    PG_V3 p0 = pg_v3(cx - 80, cy - 90, z + 2), p1 = pg_v3(cx + 80, cy - 90, z + 2), p2 = pg_v3(cx + 80, cy + 90, z + 2), p3 = pg_v3(cx - 80, cy + 90, z + 2);
    PG_Color wc = pg_rgb(255, 255, 255);
    tri_flat(sky, p0, p1, p2, wc, 0, 1, 1, 1, 1, 0, PG3D_BG | PG3D_UNLIT | PG3D_NOFOG);
    tri_flat(sky, p0, p2, p3, wc, 0, 1, 1, 0, 0, 0, PG3D_BG | PG3D_UNLIT | PG3D_NOFOG);
    /* clouds and a balloon drifting past (fade near the edges so they never poke over the frame) */
    PG_Tex cloud = art_cell(A->fx, FX_CLOUD, 4, 2), balloon = art_cell(A->fx, FX_BALLOON, 4, 2);
    for (int i = 0; i < 2; i++) {
        float span = 260.0f, xx = fmodf(t * (5.0f + 2.0f * (float)i) + 120.0f * (float)i, span) - span * 0.5f;
        float a = clampf(1.0f - fabsf(xx) / 100.0f, 0.0f, 1.0f);
        pg3d_sprite(cloud, pg_v3(cx + xx * 0.6f, cy + 38.0f - 52.0f * (float)i, z + 3), 78, 50, 0, pg_rgba(255, 255, 255, (uint8_t)(a * 255.0f)), PG3D_BG | PG3D_UNLIT | PG3D_NOFOG);
    }
    pg3d_sprite(balloon, pg_v3(cx + sinf(t * 0.17f) * 34.0f, cy + 6.0f + sinf(t * 0.4f) * 5.0f, z + 3), 46, 46, sinf(t * 0.5f) * 0.06f, pg_rgb(255, 255, 255), PG3D_BG | PG3D_UNLIT | PG3D_NOFOG);
    /* frame, mullions, sill */
    PG_Color frame = pg_hex(0xFFFFFF), sill = pg_hex(0xFFD9E6);
    PG_M4 mm;
    mm = pg_m4_translate(cx, cy + 99.0f, z + 5); pg3d_mesh(m_frame_h, A->wood, &mm, frame, PG3D_BG);
    mm = pg_m4_translate(cx, cy - 99.0f, z + 5); pg3d_mesh(m_frame_h, A->wood, &mm, frame, PG3D_BG);
    mm = pg_m4_translate(cx - 89.0f, cy, z + 5); pg3d_mesh(m_frame_v, A->wood, &mm, frame, PG3D_BG);
    mm = pg_m4_translate(cx + 89.0f, cy, z + 5); pg3d_mesh(m_frame_v, A->wood, &mm, frame, PG3D_BG);
    mm = pg_m4_translate(cx, cy, z + 5); pg3d_mesh(m_bar_h, A->wood, &mm, frame, PG3D_BG);
    mm = pg_m4_translate(cx, cy, z + 5); pg3d_mesh(m_bar_v, A->wood, &mm, frame, PG3D_BG);
    mm = pg_m4_translate(cx, cy - 112.0f, z + 15); pg3d_mesh(m_sill, A->wood, &mm, sill, PG3D_BG);
}

static void draw_shelf(void)
{
    const float sx = 262.0f, sy = 42.0f, sz = WALL_Z + 30.0f;
    PG_Color wood = pg_hex(0xFFB8CE);
    PG_M4 m;
    m = pg_m4_translate(sx - 100.0f, sy - 30.0f, sz - 4.0f); pg3d_mesh(m_brace, A->wood, &m, wood, PG3D_BG);
    m = pg_m4_translate(sx + 100.0f, sy - 30.0f, sz - 4.0f); pg3d_mesh(m_brace, A->wood, &m, wood, PG3D_BG);
    m = pg_m4_translate(sx, sy, sz); pg3d_mesh(m_board, A->wood, &m, wood, PG3D_BG);
    const float top = sy + 7.0f;
    /* alphabet blocks */
    m = pg_m4_translate(sx - 92.0f, top + 22.0f, sz); pg3d_mesh(m_block, A->block[0], &m, pg_rgb(255, 255, 255), PG3D_BG);
    m = pg_m4_translate(sx - 44.0f, top + 22.0f, sz + 4.0f); pg3d_mesh(m_block, A->block[1], &m, pg_rgb(255, 255, 255), PG3D_BG);
    m = pg_m4_mul(pg_m4_translate(sx - 68.0f, top + 66.0f, sz + 2.0f), pg_m4_rot_y(0.35f));
    pg3d_mesh(m_block, A->block[2], &m, pg_rgb(255, 255, 255), PG3D_BG);
    /* rubber duck */
    m = pg_m4_mul(pg_m4_translate(sx + 18.0f, top + 20.0f, sz), pg_m4_scale(1.25f, 0.95f, 1.0f));
    pg3d_mesh(m_duck_body, A->fur, &m, pg_hex(0xFFD93B), PG3D_BG);
    m = pg_m4_translate(sx + 36.0f, top + 48.0f, sz + 2.0f); pg3d_mesh(m_duck_head, A->fur, &m, pg_hex(0xFFD93B), PG3D_BG);
    m = pg_m4_mul(pg_m4_translate(sx + 52.0f, top + 46.0f, sz + 3.0f), pg_m4_rot_z(-1.5708f)); pg3d_mesh(m_beak, A->fur, &m, pg_hex(0xFF8A2B), PG3D_BG);
    /* beach ball */
    m = pg_m4_mul(pg_m4_translate(sx + 96.0f, top + 28.0f, sz + 2.0f), pg_m4_rot_z(0.5f));
    pg3d_mesh(m_ball, A->ball, &m, pg_rgb(255, 255, 255), PG3D_BG);
}

void scene_draw_room(float t)
{
    const PG_Color w = pg_rgb(255, 255, 255);
    pg3d_mesh(m_wall, A->wall, &ident, w, PG3D_BG);
    pg3d_mesh(m_wain, A->wain, &ident, w, PG3D_BG);
    pg3d_mesh(m_swl, A->wall, &ident, w, PG3D_BG);   pg3d_mesh(m_swl_w, A->wain, &ident, w, PG3D_BG);
    pg3d_mesh(m_swr, A->wall, &ident, w, PG3D_BG);   pg3d_mesh(m_swr_w, A->wain, &ident, w, PG3D_BG);
    pg3d_mesh(m_ceil, A->ceil_t, &ident, w, PG3D_BG);
    pg3d_mesh(m_floor, A->floor_t, &ident, w, PG3D_BG);
    PG_M4 m = pg_m4_translate(0, RAIL_Y, WALL_Z + 6.0f);
    pg3d_mesh(m_rail, A->wood, &m, pg_hex(0xFFFFFF), PG3D_BG);
    draw_window(t);
    draw_shelf();
    draw_bunting(t);
}

/* ---------------------------------------------------------------------- blob shadow */
static void shadow_quad(float cx, float cz, float rx, float rz, float alpha, PG_Color tint)
{
    PG_Tex tex = art_cell(A->fx, FX_SHADOW, 4, 2);
    const unsigned fl = PG3D_BG | PG3D_UNLIT | PG3D_TWOSIDED | PG3D_NOFOG;
    float y = SB_FLOOR_Y + 0.4f;
    tint.a = (uint8_t)(clampf(alpha, 0, 1) * 255.0f);
    PG_Vertex v[4] = {
        { cx - rx, y, cz + rz, 0, 1, 0, 0, 1, tint }, { cx + rx, y, cz + rz, 0, 1, 0, 1, 1, tint },
        { cx + rx, y, cz - rz, 0, 1, 0, 1, 0, tint }, { cx - rx, y, cz - rz, 0, 1, 0, 0, 0, tint } };
    pg3d_tri(tex, &v[0], &v[1], &v[2], fl);
    pg3d_tri(tex, &v[0], &v[2], &v[3], fl);
}

void scene_draw_shadow(const SoftBlob *b)
{
    float up = clampf(b->lowest / 240.0f, 0.0f, 0.85f);
    float cx = (b->minx + b->maxx) * 0.5f, hw = (b->maxx - b->minx) * 0.5f;
    float k = 1.0f - 0.35f * up, a = 1.0f - up;
    PG_Color tint = pg_rgb(70, 36, 104);
    float R = b->radius;
    shadow_quad(cx, 0.15f * R, (hw * 1.18f + 10.0f) * k, (0.62f * R + 6.0f) * k, 0.42f * a, tint);
    shadow_quad(cx, 0.12f * R, (hw * 0.90f + 4.0f) * k, (0.40f * R + 3.0f) * k, 0.55f * a, tint);
}

/* ------------------------------------------------------------------------ the blob */
float scene_dome_height(const SoftBlob *b) { return DOME_K * sqrtf(fmaxf(b->area, 100.0f) / PG_PI); }

static PG_V3 norm3(float x, float y, float z) { return pg_v3_norm(pg_v3(x, y, z)); }

static void vset(PG_Vertex *v, float x, float y, float z, PG_V3 n, float u, float vv, PG_Color c)
{
    v->x = x; v->y = y; v->z = z; v->nx = n.x; v->ny = n.y; v->nz = n.z; v->u = u; v->v = vv; v->c = c;
}

static void face_spr(PG_Tex tex, float fx_, float fy_, float lx, float ly, float w, float h, float z, float rot, float fw, float fh, float R, float cr, float sr, PG_Color tint)
{
    float X = fx_ + (lx * fw * cr - ly * fh * sr) * R, Y = fy_ + (lx * fw * sr + ly * fh * cr) * R;
    pg3d_sprite(tex, pg_v3(X, Y, z), w * R * fw, h * R * fh, rot, tint, 0);
}

static void draw_face(const SoftBlob *b, const BlobFx *fx, float H)
{
    const float R = b->radius;
    const float rot = b->tilt * 0.85f, cr = cosf(rot), sr = sinf(rot);
    float w = b->maxx - b->minx, h = b->maxy - b->miny;
    float fw = clampf(w / b->wrest, 0.58f, 1.28f), fh = clampf(h / b->hrest, 0.58f, 1.28f);
    /* the face sits between the belly and the middle of the skin, so a squeezed blob keeps it inside its outline */
    float fcx = 0.4f * b->x[SB_C] + 0.6f * b->cx + fx->jx, fcy = 0.4f * b->y[SB_C] + 0.6f * b->cy + fx->jy + 0.07f * R * fh;
    const float z = H + 3.0f;
    const PG_Color white = pg_rgb(255, 255, 255);
    PG_Tex sclera = art_cell(A->face, FACE_SCLERA, 4, 2), pupil = art_cell(A->face, FACE_PUPIL, 4, 2);

    /* cheeks first, then eyes, pupils, mouth: nearer sprites get a hair more z so sorting is stable */
    PG_Tex blush = art_cell(A->face, FACE_BLUSH, 4, 2);
    float blush_a = 0.80f + 0.2f * clampf(fx->giggle_t * 2.0f, 0, 1);
    face_spr(blush, fcx, fcy, -0.56f, -0.10f, 0.36f, 0.36f, z + 0.0f, rot, fw, fh, R, cr, sr, pg_rgba(255, 255, 255, (uint8_t)(blush_a * 255)));
    face_spr(blush, fcx, fcy, 0.56f, -0.10f, 0.36f, 0.36f, z + 0.0f, rot, fw, fh, R, cr, sr, pg_rgba(255, 255, 255, (uint8_t)(blush_a * 255)));

    float closed = fx->blink_p > 0 ? sinf(fx->blink_p * PG_PI) : 0.0f;       /* 0 open .. 1 shut */
    bool happy = fx->giggle_t > 0.0f;
    float ew = 0.43f * (1.0f + 0.12f * fx->wide), eh = 0.45f * (1.0f + 0.12f * fx->wide);
    for (int side = -1; side <= 1; side += 2) {
        float ex = 0.31f * (float)side, ey = 0.16f;
        if (happy) {
            face_spr(art_cell(A->face, FACE_HAPPY, 4, 2), fcx, fcy, ex, ey - 0.02f, 0.44f, 0.44f, z + 0.4f, rot, fw, fh, R, cr, sr, white);
        } else if (closed > 0.78f) {
            face_spr(art_cell(A->face, FACE_BLINK, 4, 2), fcx, fcy, ex, ey - 0.03f, 0.44f, 0.44f, z + 0.4f, rot, fw, fh, R, cr, sr, white);
        } else {
            float oh = 1.0f - 0.85f * closed;
            face_spr(sclera, fcx, fcy, ex, ey, ew, eh * oh, z + 0.4f, rot, fw, fh, R, cr, sr, white);
            float px = ex + fx->gx * 0.050f, py = ey + fx->gy * 0.070f;
            float ps = 0.30f * (1.0f - 0.10f * fx->wide);
            face_spr(pupil, fcx, fcy, px, py * oh + ey * (1.0f - oh), ps, ps * oh, z + 1.6f, rot, fw, fh, R, cr, sr, white);
        }
    }
    float my = -0.17f, mw = 0.44f, mh = 0.44f;
    if (fx->mouth == FACE_OH) { my = -0.20f; mw = 0.34f; mh = 0.40f; }
    else if (fx->mouth == FACE_GRIN) { my = -0.16f; mw = 0.48f; mh = 0.48f; }
    face_spr(art_cell(A->face, fx->mouth, 4, 2), fcx, fcy, 0.0f, my, mw, mh, z + 1.0f, rot, fw, fh, R, cr, sr, white);
}

/* Catmull-Rom midpoint between b and c (a, d are their outer neighbours) */
static inline float cr_mid(float a, float b, float c, float d) { return (-a + 9.0f * b + 9.0f * c - d) * (1.0f / 16.0f); }

void scene_draw_blob(const SoftBlob *b, const BlobFx *fx, float t)
{
    (void)t;
    enum { NR2 = SB_N * 2 };
    static PG_Vertex rim[NR2], bak[NR2], mid[SB_N];
    PG_Vertex top;
    const float Re = sqrtf(fmaxf(b->area, 100.0f) / PG_PI);
    const float H = DOME_K * Re, D = SKIRT_K * Re;
    const float ax = b->x[SB_C], ay = b->y[SB_C];
    const float inv = 0.5f / (1.12f * b->radius);
    const float zmid = H * sqrtf(1.0f - RHO * RHO);
    const PG_Color crim = pg_rgba(255, 255, 255, 236), cmid = pg_rgba(255, 255, 255, 246), ctop = pg_rgba(255, 255, 255, 250), cbak = pg_rgba(205, 195, 225, 232);

    for (int i = 0; i < SB_N; i++) {
        int j = i + 1 == SB_N ? 0 : i + 1, h = i == 0 ? SB_N - 1 : i - 1, k = j + 1 == SB_N ? 0 : j + 1;
        float ex = b->x[j] - b->x[h], ey = b->y[j] - b->y[h];
        float l = sqrtf(ex * ex + ey * ey) + 1e-4f, nx = ey / l, ny = -ex / l;
        float u = 0.5f + b->qx[i] * inv, v = 0.5f - b->qy[i] * inv;
        float um = 0.5f + RHO * b->qx[i] * inv, vm = 0.5f - RHO * b->qy[i] * inv;
        /* the rim and the skirt are smoothed to 32 points so the silhouette is round, not a 16-gon */
        vset(&rim[2 * i], b->x[i], b->y[i], 0.0f, norm3(nx * 0.92f, ny * 0.92f, 0.42f), u, v, crim);
        vset(&bak[2 * i], b->x[i] + (b->cx - b->x[i]) * 0.05f, b->y[i] + (b->cy - b->y[i]) * 0.05f, -D, norm3(nx, ny, -0.1f), u, v, cbak);
        float mx = cr_mid(b->x[h], b->x[i], b->x[j], b->x[k]), my = cr_mid(b->y[h], b->y[i], b->y[j], b->y[k]);
        float cx2 = b->x[j] - b->x[i], cy2 = b->y[j] - b->y[i], cl = sqrtf(cx2 * cx2 + cy2 * cy2) + 1e-4f, mnx = cy2 / cl, mny = -cx2 / cl;
        float u2 = 0.5f + (b->qx[i] + b->qx[j]) * 0.5f * inv, v2 = 0.5f - (b->qy[i] + b->qy[j]) * 0.5f * inv;
        vset(&rim[2 * i + 1], mx, my, 0.0f, norm3(mnx * 0.92f, mny * 0.92f, 0.42f), u2, v2, crim);
        vset(&bak[2 * i + 1], mx + (b->cx - mx) * 0.05f, my + (b->cy - my) * 0.05f, -D, norm3(mnx, mny, -0.1f), u2, v2, cbak);
        vset(&mid[i], ax + (b->x[i] - ax) * RHO, ay + (b->y[i] - ay) * RHO, zmid, norm3(nx * 0.62f, ny * 0.62f, 1.0f), um, vm, cmid);
    }
    vset(&top, ax, ay, H, norm3((b->cx - ax) * 0.02f, (b->cy - ay) * 0.02f, 1.0f), 0.5f, 0.5f, ctop);
    PG_Tex tex = A->jelly[fx->style % JB_STYLES];
    for (int i = 0; i < SB_N; i++) {
        int j = i + 1 == SB_N ? 0 : i + 1, r0 = 2 * i, r1 = 2 * i + 1, r2 = (2 * i + 2) % NR2;
        pg3d_tri(tex, &top, &mid[i], &mid[j], 0);
        pg3d_tri(tex, &mid[i], &rim[r0], &rim[r1], 0);
        pg3d_tri(tex, &mid[i], &rim[r1], &mid[j], 0);
        pg3d_tri(tex, &mid[j], &rim[r1], &rim[r2], 0);
    }
    for (int k = 0; k < NR2; k++) {
        int k1 = (k + 1) % NR2;
        pg3d_tri(tex, &rim[k], &bak[k], &bak[k1], 0);
        pg3d_tri(tex, &rim[k], &bak[k1], &rim[k1], 0);
    }

    draw_face(b, fx, H);

    /* a little leaf / sprout / antenna on top, swinging a bit */
    {
        float R0 = b->radius;
        pg3d_sprite(art_cell(A->acc, fx->style, 3, 2), pg_v3(rim[16].x, rim[16].y + 0.16f * R0, H * 0.5f), 0.50f * R0, 0.50f * R0, b->tilt + fx->acc_a, pg_rgb(255, 255, 255), 0);
    }

    /* glossy highlight: a bean and a dot on the upper-left shoulder, riding on the skin */
    const float rho_h = 0.70f, zh = H + 2.0f;
    float hx = ax + ((b->x[9] + b->x[10]) * 0.5f - ax) * rho_h, hy = ay + ((b->y[9] + b->y[10]) * 0.5f - ay) * rho_h;
    PG_Tex bean = art_cell(A->fx, FX_BEAN, 4, 2), dot = art_cell(A->fx, FX_DOT, 4, 2);
    float R = b->radius;
    pg3d_sprite(bean, pg_v3(hx, hy, zh), 0.50f * R, 0.50f * R, b->tilt, pg_rgba(255, 255, 255, 240), 0);
    pg3d_sprite(dot, pg_v3(hx + 0.20f * R, hy - 0.16f * R, zh), 0.18f * R, 0.18f * R, 0, pg_rgba(255, 255, 255, 225), 0);
    /* a faint glow inside, as if the candy were lit from behind */
    pg3d_sprite(pg3d_builtin(PG3D_TEX_GLOW), pg_v3(ax, ay - 0.05f * R, H * 0.9f), 1.5f * R, 1.3f * R, 0, pg_alpha(pg_color_lerp(art_candy(fx->style), pg_rgb(255, 255, 255), 0.30f), 0.15f), PG3D_ADDITIVE);
}
