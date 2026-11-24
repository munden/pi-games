/* scene.c - everything Block Tower draws: the toy room, the blocks, the chest and its button, juice.
 *
 * Draw order: PG3D_BG pieces (floor, rug, walls, trim, shadows) are drawn first in submission order; everything
 * else is depth sorted by pg3d. The camera sways gently so the room has parallax. */
#include "bt.h"

#include <math.h>
#include <string.h>

#define CAM_FOV 46.0f

static const PG_Color WHITE = { 255, 255, 255, 255 };

PG_Camera scene_camera(float t)
{
    PG_Camera c = pg3d_camera_2d_fov(CAM_FOV);
    c.eye.x = sinf(t * 0.31f) * 34.0f;
    c.eye.y = 48.0f + sinf(t * 0.23f) * 7.0f;
    c.target = pg_v3(0, -38.0f, 0);
    return c;
}

/* ---------------------------------------------------------------- textures */
static PG_Tex up(PG_Image *im)
{
    PG_Tex t = pg3d_texture(im);
    pg_img_free(im);
    return t;
}

/* a rectangle of a packed sheet (sw x sh texels) that lives in the shared atlas entry t */
static PG_Tex sub_tex(PG_Tex t, int x, int y, int w, int h, int sw, int sh)
{
    float du = t.u1 - t.u0, dv = t.v1 - t.v0;
    PG_Tex o = { t.u0 + du * ((float)x + 0.02f) / (float)sw, t.v0 + dv * ((float)y + 0.02f) / (float)sh,
                 t.u0 + du * ((float)(x + w) - 0.02f) / (float)sw, t.v0 + dv * ((float)(y + h) - 0.02f) / (float)sh };
    return o;
}

static void blit(PG_Image *dst, PG_Image *src, int x, int y)
{
    for (int j = 0; j < src->h; j++)
        for (int i = 0; i < src->w; i++) dst->px[(y + j) * dst->w + x + i] = src->px[j * src->w + i];
    pg_img_free(src);
}

void scene_textures(Game *g)
{
    Textures *t = &g->tex;
    for (int i = 0; i < N_CUBE_SKINS; i++) t->skin[FAM_CUBE][i] = up(paint_cube(i));
    for (int i = 0; i < N_LONG_SKINS; i++) t->skin[FAM_LONG][i] = up(paint_long(i));
    for (int i = 0; i < N_TALL_SKINS; i++) t->skin[FAM_TALL][i] = up(paint_tall(i));
    for (int i = 0; i < N_FLAT_SKINS; i++) t->skin[FAM_FLAT][i] = up(paint_flat(i));
    t->ball[0] = up(paint_ball(0)); t->ball[1] = up(paint_ball(1));
    t->floor = up(paint_floor());
    t->wall = up(paint_wallpaper());
    t->rug = up(paint_rug());
    t->view = up(paint_view());
    t->chest = up(paint_chest_front());
    t->poster = up(paint_poster());
    /* small props share one 128x128 atlas entry: glove, button, wainscot boards, grain */
    PG_Image *sheet = pg_img_new(128, 128, 0x00FFFFFFu);
    blit(sheet, paint_glove(), 0, 0);
    blit(sheet, paint_button(), 64, 0);
    blit(sheet, paint_wainscot(), 0, 64);
    blit(sheet, paint_fabric(), 64, 64);
    PG_Tex ts = up(sheet);
    t->glove = sub_tex(ts, 0, 0, 64, 64, 128, 128);
    t->button = sub_tex(ts, 64, 0, 64, 64, 128, 128);
    t->wainscot = sub_tex(ts, 0, 64, 64, 64, 128, 128);
    t->fabric = sub_tex(ts, 64, 64, 16, 16, 128, 128);
    pg3d_sky(pg_hex(0xFFE9C8), pg_hex(0xFFD7A0));
    pg3d_light(pg_v3(0.55f, 0.65f, 0.60f), pg_hex(0xA29882), pg_hex(0x8E92AC), pg_hex(0x665C54));
    pg3d_fog(pg_hex(0xFFEBD2), 760.0f, 1500.0f);
}

/* ----------------------------------------------------------------- helpers */
static void put(const PG_Mesh *m, PG_Tex tex, float x, float y, float z, float sx, float sy, float sz, float ang, PG_Color tint, unsigned fl)
{
    PG_M4 mm = pg_m4_trs2(x, y, z, ang, sx, sy, sz);
    pg3d_mesh(m, tex, &mm, tint, fl);
}

static void put_wall(const PG_Mesh *m, PG_Tex tex, float x, float y, float z, float yaw, unsigned fl)
{
    PG_M4 mm = pg_m4_mul(pg_m4_translate(x, y, z), pg_m4_rot_y(yaw));
    pg3d_mesh(m, tex, &mm, WHITE, fl);
}

static PG_Tex skin_tex(const Game *g, int kind, int type, int skin)
{
    if (kind == KIND_BALL) return g->tex.ball[skin & 1];
    int fam = bt_shape(type)->family;
    int n = fam == FAM_CUBE ? N_CUBE_SKINS : fam == FAM_LONG ? N_LONG_SKINS : fam == FAM_TALL ? N_TALL_SKINS : N_FLAT_SKINS;
    return g->tex.skin[fam][skin % n];
}

static void draw_thing(const Game *g, int kind, int type, int skin, float x, float y, float z, float ang, float sx, float sy, float sz, float shade, float alpha)
{
    uint8_t s = (uint8_t)(255.0f * bt_clampf(shade, 0, 1));
    PG_Color tint = pg_rgba(s, s, s, (uint8_t)(255.0f * bt_clampf(alpha, 0, 1)));
    if (kind == KIND_BALL) {
        put(g->mesh.ball, skin_tex(g, kind, type, skin), x, y, z, sx, sy, sz, ang, tint, 0);
    } else {
        put(g->mesh.shape[type], skin_tex(g, kind, type, skin), x, y, z, sx, sy, sz, ang, tint, 0);
    }
}

/* ---------------------------------------------------------------- the room */
static void draw_toys(const Game *g)
{
    const Meshes *m = &g->mesh;
    const PG_Tex f = g->tex.fabric;
    const float ys = 106.0f, z = BACK_Z + 30.0f;
    /* mini alphabet blocks */
    put(m->mini, g->tex.skin[FAM_CUBE][0], 118, ys + 16, z, 1, 1, 1, 0.03f, WHITE, 0);
    put(m->mini, g->tex.skin[FAM_CUBE][1], 153, ys + 16, z, 1, 1, 1, -0.02f, WHITE, 0);
    put(m->mini, g->tex.skin[FAM_CUBE][3], 135, ys + 48, z, 1, 1, 1, 0.08f, WHITE, 0);
    /* teddy bear */
    const PG_Color brown = pg_rgb(196, 132, 74), tan = pg_rgb(238, 200, 146);
    float bx = 215;
    put(m->sph_lo, f, bx - 21, ys + 16, z + 22, 10, 10, 10, 0, brown, 0);          /* legs */
    put(m->sph_lo, f, bx + 21, ys + 16, z + 22, 10, 10, 10, 0, brown, 0);
    put(m->sph, f, bx, ys + 27, z, 25, 27, 22, 0, brown, 0);                    /* body */
    put(m->sph, f, bx, ys + 30, z + 14, 15, 17, 12, 0, tan, 0);                 /* tummy */
    put(m->sph_lo, f, bx - 27, ys + 32, z + 4, 8, 11, 8, 0.5f, brown, 0);          /* arms */
    put(m->sph_lo, f, bx + 27, ys + 32, z + 4, 8, 11, 8, -0.5f, brown, 0);
    put(m->sph_lo, f, bx - 14, ys + 65, z - 2, 7, 7, 6, 0, brown, 0);              /* ears */
    put(m->sph_lo, f, bx + 14, ys + 65, z - 2, 7, 7, 6, 0, brown, 0);
    put(m->sph, f, bx, ys + 56, z + 2, 19, 17, 17, 0, brown, 0);                /* head */
    put(m->sph_lo, f, bx, ys + 51, z + 15, 9, 7, 6, 0, tan, 0);                    /* muzzle */
    put(m->sph_lo, f, bx, ys + 54, z + 20, 3, 2.4f, 2.4f, 0, pg_rgb(60, 40, 40), 0);
    put(m->sph_lo, f, bx - 7, ys + 60, z + 15, 2.4f, 2.4f, 2, 0, pg_rgb(40, 34, 40), 0);
    put(m->sph_lo, f, bx + 7, ys + 60, z + 15, 2.4f, 2.4f, 2, 0, pg_rgb(40, 34, 40), 0);
    put(m->torus, f, bx, ys + 41, z + 1, 14, 6, 14, 0, pg_rgb(232, 67, 58), 0);   /* a red bow */
    /* rubber duck */
    float dx = 282;
    put(m->sph, f, dx, ys + 14, z + 4, 20, 14, 15, 0, pg_rgb(255, 214, 59), 0);
    put(m->sph_lo, f, dx - 17, ys + 22, z + 4, 8, 9, 6, 0.4f, pg_rgb(255, 214, 59), 0);   /* tail */
    put(m->sph_lo, f, dx + 10, ys + 33, z + 5, 11, 11, 11, 0, pg_rgb(255, 214, 59), 0);
    put(m->cone, f, dx + 22, ys + 31, z + 5, 4, 7, 5, -1.45f, pg_rgb(255, 138, 42), 0);
    put(m->sph_lo, f, dx + 13, ys + 37, z + 14, 2.2f, 2.2f, 2, 0, pg_rgb(40, 34, 40), 0);
    /* ring stacker */
    float rx = 338;
    put(m->cyl, f, rx, ys + 4, z, 17, 4, 17, 0, pg_rgb(62, 134, 232), 0);
    put(m->cyl, f, rx, ys + 24, z, 2.6f, 20, 2.6f, 0, pg_rgb(255, 230, 190), 0);
    static const uint32_t ring[4] = { 0xE8433A, 0xFF9A2E, 0xFFD93B, 0x4CB84A };
    for (int i = 0; i < 4; i++) put(m->torus, f, rx, ys + 11 + (float)i * 8.5f, z, 17.0f - (float)i * 2.6f, 11, 17.0f - (float)i * 2.6f, 0, pg_hex(ring[i]), 0);
    put(m->sph_lo, f, rx, ys + 46, z, 4, 4, 4, 0, pg_rgb(255, 230, 190), 0);
}

static void draw_bunting(const Game *g, float t)
{
    static const uint32_t cols[5] = { 0xE8433A, 0xFFC72C, 0x3E86E8, 0x4CB84A, 0xFF8FB8 };
    const PG_Tex f = g->tex.fabric;
    const float zb = BACK_Z + 12.0f;
    enum { N = 17 };
    float px[N + 1], py[N + 1];
    for (int i = 0; i <= N; i++) {
        float u = (float)i / (float)N;
        px[i] = -432.0f + 864.0f * u;
        py[i] = 292.0f - 46.0f * sinf(u * PG_PI);
    }
    PG_Color rope = pg_rgb(255, 244, 226);
    for (int i = 0; i < N; i++) {                         /* the rope */
        PG_Vertex a = { px[i], py[i] + 1.5f, zb, 0, 0, 1, 0, 0, rope }, b = { px[i + 1], py[i + 1] + 1.5f, zb, 0, 0, 1, 1, 0, rope };
        PG_Vertex c = { px[i + 1], py[i + 1] - 1.5f, zb, 0, 0, 1, 1, 1, rope }, d = { px[i], py[i] - 1.5f, zb, 0, 0, 1, 0, 1, rope };
        pg3d_tri(f, &a, &b, &c, PG3D_TWOSIDED); pg3d_tri(f, &a, &c, &d, PG3D_TWOSIDED);
    }
    for (int i = 0; i < N; i++) {                         /* pennants swaying a little */
        float mx = (px[i] + px[i + 1]) * 0.5f, my = (py[i] + py[i + 1]) * 0.5f;
        float sw = sinf(t * 1.5f + (float)i * 0.9f) * 0.16f;
        PG_Color c = pg_hex(cols[i % 5]);
        PG_Vertex a = { px[i] + 3, py[i] - 1, zb + 1, 0, 0, 1, 0, 0, c };
        PG_Vertex b = { px[i + 1] - 3, py[i + 1] - 1, zb + 1, 0, 0, 1, 1, 0, c };
        PG_Vertex tip = { mx + 40.0f * sinf(sw), my - 44.0f * cosf(sw), zb + 2, 0, 0, 1, 0.5f, 1, c };
        pg3d_tri(f, &a, &b, &tip, PG3D_TWOSIDED);
    }
}

static void draw_window(const Game *g)
{
    const Meshes *m = &g->mesh;
    const PG_Tex f = g->tex.fabric;
    const float wx = -175, wy = 120;
    const PG_Color paint = pg_rgb(255, 248, 232), coral = pg_rgb(255, 150, 164);
    put(m->frame_h, f, wx, wy + 93, BACK_Z + 10, 1, 1, 1, 0, paint, 0);
    put(m->frame_v, f, wx - 103, wy, BACK_Z + 10, 1, 1, 1, 0, paint, 0);
    put(m->frame_v, f, wx + 103, wy, BACK_Z + 10, 1, 1, 1, 0, paint, 0);
    put(m->bar_h, f, wx, wy, BACK_Z + 6, 1, 1, 1, 0, paint, 0);
    put(m->bar_v, f, wx, wy, BACK_Z + 6, 1, 1, 1, 0, paint, 0);
    put(m->sill, f, wx, wy - 91, BACK_Z + 20, 1, 1, 1, 0, paint, 0);
    put(m->rod, f, wx, wy + 117, BACK_Z + 16, 1, 1, 1, 0, pg_rgb(214, 160, 90), 0);
    put(m->sph_lo, f, wx - 126, wy + 117, BACK_Z + 16, 7, 7, 7, 0, pg_rgb(214, 160, 90), 0);
    put(m->sph_lo, f, wx + 126, wy + 117, BACK_Z + 16, 7, 7, 7, 0, pg_rgb(214, 160, 90), 0);
    put(m->curtain, f, wx - 132, wy + 13, BACK_Z + 16, 1, 1, 1, 0, coral, PG3D_TWOSIDED);
    put(m->curtain, f, wx + 132, wy + 13, BACK_Z + 16, 1, 1, 1, 0, coral, PG3D_TWOSIDED);
    put(m->torus, f, wx - 132, wy - 20, BACK_Z + 18, 22, 9, 22, 0, pg_rgb(255, 214, 90), PG3D_TWOSIDED);
    put(m->torus, f, wx + 132, wy - 20, BACK_Z + 18, 22, 9, 22, 0, pg_rgb(255, 214, 90), PG3D_TWOSIDED);
}

static void draw_room(const Game *g, float t)
{
    const Meshes *m = &g->mesh;
    const Textures *x = &g->tex;
    const PG_Color cream = pg_rgb(255, 242, 222);

    for (int iz = 0; iz < 4; iz++)
        for (int ix = 0; ix < 4; ix++)
            put(m->floor_tile, x->floor, -384.0f + 256.0f * (float)ix, FLOOR_Y, -226.0f + 128.0f * (float)iz, 1, 1, 1, 0, WHITE, PG3D_BG);
    put(m->rug, x->rug, -30, FLOOR_Y + 0.3f, -5, 292, 1, 130, 0, WHITE, PG3D_BG);

    for (int row = 0; row < 4; row++)                       /* back wall */
        for (int col = 0; col < 6; col++)
            put(m->back_tile, x->wall, -400.0f + 160.0f * (float)col, FLOOR_Y + 190.0f + 160.0f * (float)row, BACK_Z, 1, 1, 1, 0, WHITE, PG3D_BG);
    for (int col = 0; col < 7; col++) put(m->wain_tile, x->wainscot, -384.0f + 128.0f * (float)col, FLOOR_Y + 55, BACK_Z + 0.5f, 1, 1, 1, 0, WHITE, PG3D_BG);
    for (int s = 0; s < 2; s++) {                           /* side walls */
        float sx = s ? WALL_X : -WALL_X, yaw = s ? -PG_PI * 0.5f : PG_PI * 0.5f;
        for (int row = 0; row < 4; row++)
            for (int i = 0; i < 3; i++)
                put_wall(m->wall_tile, x->wall, sx, FLOOR_Y + 190.0f + 160.0f * (float)row, BACK_Z + 80.0f + 160.0f * (float)i, yaw, PG3D_BG);
        for (int i = 0; i < 4; i++) put_wall(m->wain_tile, x->wainscot, sx, FLOOR_Y + 55, BACK_Z + 64.0f + 128.0f * (float)i, yaw, PG3D_BG);
    }
    put(m->baseboard, x->fabric, 0, FLOOR_Y + 13, BACK_Z + 5, 1, 1, 1, 0, cream, PG3D_BG);
    put(m->rail, x->fabric, 0, FLOOR_Y + 112, BACK_Z + 7, 1, 1, 1, 0, cream, PG3D_BG);
    put(m->side_base, x->fabric, -WALL_X + 5, FLOOR_Y + 13, -75, 1, 1, 1, 0, cream, PG3D_BG);
    put(m->side_base, x->fabric, WALL_X - 5, FLOOR_Y + 13, -75, 1, 1, 1, 0, cream, PG3D_BG);
    put(m->side_rail, x->fabric, -WALL_X + 7, FLOOR_Y + 112, -75, 1, 1, 1, 0, cream, PG3D_BG);
    put(m->side_rail, x->fabric, WALL_X - 7, FLOOR_Y + 112, -75, 1, 1, 1, 0, cream, PG3D_BG);

    put(m->quad, x->view, -175, 120, BACK_Z + 1, 95, 85, 1, 0, WHITE, PG3D_BG | PG3D_UNLIT | PG3D_NOFOG);
    put(m->poster_frame, x->fabric, -388, 118, BACK_Z + 5, 1.2f, 1.2f, 1, 0.03f, pg_rgb(255, 214, 120), 0);     /* a framed painting */
    put(m->poster, x->poster, -388, 118, BACK_Z + 10, 1.2f, 1.2f, 1, 0.03f, WHITE, 0);
    draw_window(g);
    /* shelf with toys */
    put(m->shelf, x->fabric, 228, 100, BACK_Z + 34, 1, 1, 1, 0, pg_rgb(214, 150, 84), 0);
    put(m->bracket, x->fabric, 130, 77, BACK_Z + 26, 1, 1, 1, 0, pg_rgb(214, 150, 84), 0);
    put(m->bracket, x->fabric, 326, 77, BACK_Z + 26, 1, 1, 1, 0, pg_rgb(214, 150, 84), 0);
    draw_toys(g);
    draw_bunting(g, t);
}

/* -------------------------------------------------------------- shadows */
static void draw_shadows(const Game *g)
{
    for (int i = 0; i < MAX_BLOCKS; i++) {
        const Block *b = &g->blocks[i];
        if (!b->body) continue;
        const PG_Body *p = b->body;
        float ex, ey;
        if (b->kind == KIND_BOX) {
            const ShapeDef *s = bt_shape(b->type);
            ex = s->hx * fabsf(p->c) + s->hy * fabsf(p->s);
            ey = s->hx * fabsf(p->s) + s->hy * fabsf(p->c);
        } else ex = ey = p->radius;
        float surf = (fabsf(p->x - CHEST_X) < CHEST_HX + ex * 0.4f && p->y > CHEST_TOP - 8.0f) ? CHEST_TOP : FLOOR_Y;
        float h = fmaxf(0, p->y - ey - surf);
        if (h > 300.0f) continue;
        float a = 0.52f / (1.0f + h * 0.028f);
        if (a < 0.06f) continue;
        float rad = fmaxf(ex * 0.85f, 30.0f) * (1.0f + h / 600.0f);
        int nb = ex > 70.0f ? 3 : ex > 44.0f ? 2 : 1;
        for (int k = 0; k < nb; k++) {
            float dx = ((float)k - (float)(nb - 1) * 0.5f) * ex * 0.85f;
            pg3d_blob_shadow(p->x + dx + h * 0.10f, surf + 0.8f, -h * 0.07f, rad / (float)(nb > 1 ? 1.35f : 1.0f), a);
        }
    }
    pg3d_blob_shadow(CHEST_X - 6, FLOOR_Y + 0.8f, 6, 130, 0.45f);
}

/* ------------------------------------------------------------ the chest */
static void draw_chest(const Game *g, float t)
{
    const Meshes *m = &g->mesh;
    const Textures *x = &g->tex;
    float hop = g->lid_kick * 8.0f;
    put(m->chest_body, x->fabric, CHEST_X, FLOOR_Y + 58, 0, 1, 1, 1, 0, pg_rgb(70, 130, 222), 0);
    put(m->chest_plate, x->chest, CHEST_X, FLOOR_Y + 58, CHEST_HZ + 3.0f, 1, 1, 1, 0, WHITE, 0);
    put(m->chest_lid, x->fabric, CHEST_X, FLOOR_Y + 128 + hop, 0, 1, 1, 1, 0, pg_rgb(255, 206, 78), 0);
    float pr = g->press, s = (1.0f + 0.03f * sinf(t * 3.4f)) * (1.0f - 0.10f * pr), zo = -9.0f * pr;
    put(m->btn_rim, x->fabric, CHEST_X, BTN_Y, BTN_Z + zo, s, s, s, 0, pg_rgb(255, 214, 92), 0);
    put(m->btn_face, x->button, CHEST_X, BTN_Y, BTN_Z + 3.0f + zo, s, s, s, 0, WHITE, PG3D_UNLIT | PG3D_NOFOG);
}


/* a toy car left lying in the foreground (in front of the play plane): depth and scale for the eye.
 * Only the near wheels are drawn, and they sit beside the body, never inside it (the painter's sort needs that). */
static void draw_car(const Game *g)
{
    const Meshes *m = &g->mesh;
    const PG_Tex f = g->tex.fabric;
    const float x = -292.0f, z = 188.0f, y0 = FLOOR_Y;
    put(m->car_body, f, x, y0 + 19, z, 1, 1, 1, 0, pg_rgb(232, 67, 58), 0);
    put(m->car_cab, f, x - 6, y0 + 40, z, 1, 1, 1, 0, pg_rgb(255, 210, 70), 0);
    put(m->sph_lo, f, x + 39, y0 + 21, z + 13, 5, 5, 3, 0, pg_rgb(255, 248, 200), 0);          /* headlamps */
    put(m->sph_lo, f, x + 39, y0 + 21, z - 13, 5, 5, 3, 0, pg_rgb(255, 248, 200), 0);
    for (int i = 0; i < 2; i++) {
        float wx = x + (i ? 24.0f : -24.0f), wz = z + 23.0f + 6.5f;
        put(m->wheel, f, wx, y0 + 10, wz, 10, 10, 6, 0, pg_rgb(70, 74, 96), 0);
        put(m->btn_face, f, wx, y0 + 10, wz + 6.4f, 0.1f, 0.1f, 1, 0, pg_rgb(210, 214, 230), 0);
    }
}

/* ---------------------------------------------------------------- blocks */
static float pop_scale(float p)         /* 0..1 -> 0.55 .. 1.08 .. 1 */
{
    if (p >= 1.0f) return 1.0f;
    float e = p < 0.7f ? 0.55f + 0.53f * (p / 0.7f) : 1.08f - 0.08f * ((p - 0.7f) / 0.3f);
    return e;
}

static void draw_blocks(const Game *g)
{
    for (int i = 0; i < MAX_BLOCKS; i++) {
        const Block *b = &g->blocks[i];
        if (!b->body) continue;
        const PG_Body *p = b->body;
        float k = pop_scale(b->pop), sq = b->squash, sx = 1, sy = 1, z = b->lift * 22.0f;
        float shade = b->shade + b->lift * 0.08f;
        if (b->kind == KIND_BOX) {
            if (b->squash_axis == 0) { sy = 1.0f - sq; sx = 1.0f + sq * 0.55f; } else { sx = 1.0f - sq; sy = 1.0f + sq * 0.55f; }
            draw_thing(g, KIND_BOX, b->type, b->skin, p->x, p->y, z, p->angle, sx * k, sy * k, k, shade, 1.0f);
        } else {
            float r = p->radius;
            draw_thing(g, KIND_BALL, 0, b->skin, p->x, p->y, z, p->angle, r * k, r * k, r * k, 1.0f, 1.0f);
            pg3d_shine(pg_v3(p->x, p->y, z), r * k, 0.8f);
        }
    }
}

static void draw_ghosts(const Game *g)
{
    for (int i = 0; i < MAX_GHOSTS; i++) {
        const Ghost *gh = &g->ghosts[i];
        if (gh->mode == GH_NONE || gh->t < 0) continue;
        float x = gh->x1, y = gh->y1, ang = gh->ang, sc = 1, al = 1;
        if (gh->mode == GH_TIDY) {
            float u = bt_clampf(gh->t / gh->dur, 0, 1), e = u * u * (3.0f - 2.0f * u), v = 1.0f - e;
            x = v * v * gh->x0 + 2 * v * e * gh->cx + e * e * gh->x1;
            y = v * v * gh->y0 + 2 * v * e * gh->cy + e * e * gh->y1;
            ang = gh->ang + gh->spin * e;
            sc = 1.0f - 0.7f * e * e;
            al = 1.0f - bt_clampf((u - 0.85f) / 0.15f, 0, 1);
        } else if (gh->mode == GH_RAIN) {
            y = fmaxf(gh->y1, gh->y0 - bt_fall_dist(gh->t));
            sc = pop_scale(bt_clampf(gh->t * 4.0f, 0, 1));
        } else {
            float u = bt_clampf(gh->t / gh->dur, 0, 1);
            y = gh->y0 + 30.0f * u; ang = gh->ang + gh->spin * u * 3.0f;
            sc = 1.0f - u * u; al = 1.0f - u;
        }
        float z = gh->mode == GH_TIDY ? 30.0f : 0.0f;
        if (gh->kind == KIND_BALL) {
            float r = bt_ball_r(gh->skin);
            draw_thing(g, KIND_BALL, 0, gh->skin, x, y, z, ang, r * sc, r * sc, r * sc, 1.0f, al);
        } else draw_thing(g, KIND_BOX, gh->type, gh->skin, x, y, z, ang, sc, sc, sc, gh->shade, al);
    }
}

/* ------------------------------------------------------------------ juice */
static void draw_fx(const Game *g, float t)
{
    PG_Tex star = pg3d_builtin(PG3D_TEX_STAR), glow = pg3d_builtin(PG3D_TEX_GLOW), disc = pg3d_builtin(PG3D_TEX_DISC);
    for (int i = 0; i < MAX_PARTS; i++) {
        const Part *p = &g->parts[i];
        if (p->kind == PT_NONE) continue;
        float k = p->life / p->max;
        if (p->kind == PT_STAR) {
            float s = p->size * bt_clampf(p->life * 4.0f, 0, 1);
            PG_Color c = pg_hsv(p->hue, 0.5f, 1.0f);
            pg3d_sprite(star, p->p, s, s, p->rot, c, 0);
            pg3d_sprite(glow, p->p, s * 2.2f, s * 2.2f, 0, pg_alpha(c, 0.55f * k), PG3D_ADDITIVE);
        } else {
            float a = 0.72f * bt_clampf(k * 1.8f, 0, 1);          /* flat cartoon puffs: a few overlapping discs */
            float s = p->size;
            pg3d_sprite(disc, p->p, s, s, 0, pg_rgba(232, 216, 190, (uint8_t)(255.0f * a)), 0);
            pg3d_sprite(disc, pg_v3(p->p.x + s * 0.16f, p->p.y + s * 0.12f, p->p.z + 1), s * 0.62f, s * 0.62f, 0, pg_rgba(248, 238, 218, (uint8_t)(255.0f * a)), 0);
        }
    }
    /* soft light around every finger; held blocks glow */
    const PG_Touch *tc = pg_touches();
    for (int i = 0; i < PG_MAX_TOUCH; i++) {
        if (tc[i].state == PG_TOUCH_NONE || tc[i].state == PG_TOUCH_UP) continue;
        PG_V3 w;
        if (!pg3d_plane_z(tc[i].x, tc[i].y, 80.0f, &w)) continue;
        PG_Color c = pg_hsv(30.0f + 47.0f * (float)i, 0.35f, 1.0f);
        pg3d_sprite(glow, w, 90, 90, 0, pg_alpha(c, 0.5f), PG3D_ADDITIVE);
        pg3d_sprite(glow, w, 36, 36, 0, pg_alpha(WHITE, 0.55f), PG3D_ADDITIVE);
    }
    for (int i = 0; i < MAX_BLOCKS; i++) {
        const Block *b = &g->blocks[i];
        if (!b->body || b->lift < 0.05f) continue;
        float r = b->kind == KIND_BALL ? b->body->radius * 2.4f : hypotf(b->body->hx, b->body->hy) * 2.5f;
        pg3d_sprite(glow, pg_v3(b->body->x, b->body->y, 26), r * 1.15f, r * 1.15f, 0, pg_alpha(pg_rgb(255, 232, 170), 0.2f * b->lift), PG3D_ADDITIVE);
    }
    /* the big button breathes so it is easy to find; the sun twinkles */
    float pulse = 0.5f + 0.5f * sinf(t * 2.2f);
    pg3d_sprite(glow, pg_v3(CHEST_X, BTN_Y, BTN_Z + 16), 190 + 14 * pulse, 190 + 14 * pulse, 0,
                pg_alpha(pg_rgb(255, 170, 110), 0.16f + 0.10f * pulse + 0.5f * g->press), PG3D_ADDITIVE);
    pg3d_sprite(glow, pg_v3(-135, 163, BACK_Z + 4), 100 + 10 * sinf(t * 1.1f), 100 + 10 * sinf(t * 1.1f), 0, pg_alpha(pg_rgb(255, 236, 150), 0.35f), PG3D_ADDITIVE);

    /* dust motes drifting in the window light: the room is never still */
    for (int i = 0; i < 14; i++) {
        float fi = (float)i, ph = fi * 2.399f;
        float mx = -200.0f + 150.0f * sinf(ph) + 40.0f * sinf(t * 0.21f + ph * 1.7f), my = 40.0f + 130.0f * cosf(ph * 1.3f) + 30.0f * sinf(t * 0.27f + ph);
        float tw = 0.5f + 0.5f * sinf(t * 1.3f + ph * 3.1f);
        pg3d_sprite(glow, pg_v3(mx, my, -90.0f + 120.0f * sinf(ph * 0.7f)), 9, 9, 0, pg_alpha(pg_rgb(255, 244, 205), 0.25f + 0.45f * tw), PG3D_ADDITIVE);
    }

    /* the pointing hand, with a soft ring where it points */
    if (g->hand_vis > 0.02f) {
        float bob = sinf(t * 3.1f) * 6.0f, dip = g->hand_tap * 34.0f;
        float tipx = g->hand_x, tipy = g->hand_y + bob - dip, size = 112.0f, roll = 0.32f;
        float cx = tipx - 0.44f * size * sinf(roll), cy = tipy - 0.44f * size * cosf(roll);
        PG_Color c = pg_alpha(WHITE, g->hand_vis);
        pg3d_sprite(glow, pg_v3(g->hand_tx, g->hand_ty - 6, 66), 120 + 20 * sinf(t * 4.0f), 120 + 20 * sinf(t * 4.0f), 0, pg_alpha(pg_rgb(255, 240, 170), 0.45f * g->hand_vis), PG3D_ADDITIVE);
        pg3d_sprite(g->tex.glove, pg_v3(cx, cy, 90), size, size, -roll, c, 0);
    }
}

/* ------------------------------------------------------------------ frame */
void scene_render(Game *g, SDL_Renderer *r)
{
    float t = pg_time();
    PG_Camera cam = scene_camera(t);
    pg3d_begin(&cam);
    draw_room(g, t);
    draw_shadows(g);
    draw_chest(g, t);
    draw_car(g);
    draw_blocks(g);
    draw_ghosts(g);
    draw_fx(g, t);
    pg3d_end(r);
}
