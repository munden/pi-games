/* _template - the smallest complete game, and the visual reference for "PS1 or better".
 *
 * Shows the whole loop: build textures + meshes in init(), react to touch in update(), draw a lit,
 * textured, sorted 3D scene in render(). Copy with `make new GAME=my-game`, then replace the toys.
 * Rules you must keep (AGENTS.md): touch only, no text needed, instant response, gentle sound,
 * something always moving when idle, nothing can be "lost" or "wrong".
 */
#include "pg3d.h"

#define MAX_SPARKS 96
#define FLOOR_Y   (-230.0f)
#define WALL_Z    (-300.0f)

typedef struct { PG_V3 p, v; float life, hue, size; } Spark;

static PG_Mesh *m_block, *m_ball, *m_cone, *m_drum, *m_ring, *m_wall, *m_floor;
static PG_Tex t_block, t_ball, t_cone, t_drum, t_ring, t_wall, t_floor;
static Spark sparks[MAX_SPARKS];

/* ---- textures are painted, never loaded: small, chunky, 15-bit dithered like a PS1 ---- */
static PG_Tex paint_block(void)
{
    PG_Image *im = pg_img_new(64, 64, PG_RGB24(0xE9B26A));
    for (int x = 0; x < 64; x += 5) pg_img_rect(im, x, 0, 2, 64, PG_ARGB(40, 150, 90, 30));   /* wood grain */
    pg_img_noise(im, 10, 7);
    pg_img_frame(im, 0, 0, 64, 64, 3, PG_RGB24(0xB97A3A));
    pg_img_text(im, 17, 11, 6, PG_RGB24(0xD8402F), "A");
    pg_img_ps1(im);
    PG_Tex t = pg3d_texture(im); pg_img_free(im); return t;
}

static PG_Tex paint_ball(void)
{
    static const uint32_t col[6] = { 0xF0463C, 0xFFFFFF, 0x2D7DF0, 0xFFFFFF, 0xFFC928, 0xFFFFFF };
    PG_Image *im = pg_img_new(64, 64, 0xFFFFFFFFu);
    for (int i = 0; i < 6; i++) pg_img_rect(im, i * 64 / 6, 0, 64 / 6 + 1, 64, PG_RGB24(col[i]));
    pg_img_ps1(im);
    PG_Tex t = pg3d_texture(im); pg_img_free(im); return t;
}

static PG_Tex paint_stripes(uint32_t a, uint32_t b, int w)
{
    PG_Image *im = pg_img_new(64, 64, PG_RGB24(a));
    pg_img_stripes(im, w, 0, PG_RGB24(a), PG_RGB24(b));
    pg_img_noise(im, 6, 3);
    pg_img_ps1(im);
    PG_Tex t = pg3d_texture(im); pg_img_free(im); return t;
}

static PG_Tex paint_floor(void)
{
    PG_Image *im = pg_img_new(64, 64, 0);
    pg_img_checker(im, 16, PG_RGB24(0x7FC77A), PG_RGB24(0x6AB367));
    pg_img_noise(im, 8, 11);
    pg_img_ps1(im);
    PG_Tex t = pg3d_texture(im); pg_img_free(im); return t;
}

static PG_Tex paint_wall(void)
{
    PG_Image *im = pg_img_new(128, 64, PG_RGB24(0xFFE8B8));
    pg_img_gradient_v(im, 0, 0, 128, 64, PG_RGB24(0xFFF1CF), PG_RGB24(0xFFD79A));
    for (int i = 0; i < 6; i++) pg_img_star(im, 12.0f + 21.0f * (float)i, 18.0f + 14.0f * (float)(i & 1), 7, 3, 5, 0.2f * (float)i, PG_RGB24(0xFFFFFF));
    pg_img_noise(im, 5, 5);
    pg_img_ps1(im);
    PG_Tex t = pg3d_texture(im); pg_img_free(im); return t;
}

static int init(SDL_Renderer *r)
{
    (void)r;
    t_block = paint_block();   t_ball = paint_ball();
    t_cone  = paint_stripes(0xFF6FB5, 0xFFD1E8, 8);
    t_drum  = paint_stripes(0x4DA6FF, 0x8CC8FF, 10);
    t_ring  = paint_stripes(0xFFC928, 0xFF9F1C, 6);
    t_floor = paint_floor();   t_wall = paint_wall();

    m_block = pg_mesh_bevel_box(70, 70, 55, 14);
    m_ball  = pg_mesh_sphere(70, 14, 10);
    m_cone  = pg_mesh_cone(60, 70, 12);
    m_drum  = pg_mesh_cylinder(60, 45, 14);
    m_ring  = pg_mesh_torus(48, 17, 16, 8);
    m_wall  = pg_mesh_quad(920, 380);
    m_floor = pg_mesh_floor(700, 520, 8, 8);

    pg3d_sky(pg_hex(0x7EC8FF), pg_hex(0xFFE9C2));
    pg3d_light(pg_v3(0.5f, 0.8f, 0.6f), pg_hex(0xB8B2A4), pg_hex(0x8C96B0), pg_hex(0x6A645C));
    return 0;
}

static void spawn(float x, float y)
{
    for (int n = 0, i = 0; n < 10 && i < MAX_SPARKS; i++) {
        if (sparks[i].life > 0) continue;
        float a = pg_rand_range(0, PG_TAU), s = pg_rand_range(80, 260);
        sparks[i] = (Spark){ pg_v3(x, y, 40), pg_v3(cosf(a) * s, sinf(a) * s + 120, pg_rand_range(-30, 30)), 1.2f, pg_rand_range(0, 360), pg_rand_range(22, 40) };
        n++;
    }
}

static void update(void)
{
    const PG_Touch *t = pg_touches();
    for (int i = 0; i < PG_MAX_TOUCH; i++) {
        if (t[i].state == PG_TOUCH_NONE) continue;
        PG_V3 w;
        if (!pg3d_plane_z(t[i].x, t[i].y, 0, &w)) continue;
        if (t[i].state == PG_TOUCH_DOWN) { spawn(w.x, w.y); pg_sfx_chime(pg_rand_int(0, 9), 0.7f); }
        else if (t[i].state == PG_TOUCH_HELD && (t[i].ticks % 4) == 0) spawn(w.x, w.y);
    }
    for (int i = 0; i < MAX_SPARKS; i++) {
        Spark *s = &sparks[i];
        if (s->life <= 0) continue;
        s->v.y -= 500 * PG_DT; s->p = pg_v3_add(s->p, pg_v3_scale(s->v, PG_DT)); s->life -= PG_DT;
    }
}

static void render(SDL_Renderer *r)
{
    float tm = pg_time();
    PG_Camera cam = pg3d_camera_2d_fov(48);
    cam.eye.x = sinf(tm * 0.35f) * 50; cam.eye.y = 60 + sinf(tm * 0.23f) * 14; cam.target.y = -30;
    pg3d_begin(&cam);

    /* backdrop and room (BG = drawn first, unsorted) */
    PG_M4 mw = pg_m4_translate(0, 20, WALL_Z), mf = pg_m4_translate(0, FLOOR_Y, -120);
    pg3d_mesh(m_wall, t_wall, &mw, pg_rgb(255, 255, 255), PG3D_BG);
    pg3d_mesh(m_floor, t_floor, &mf, pg_rgb(255, 255, 255), PG3D_BG);

    /* the toys: bobbing, spinning, with blob shadows on the floor */
    struct { float x, y, z; } pos[5] = {
        { -270, -150 + sinf(tm * 1.6f) * 14, 0 }, { 0, -60 + sinf(tm * 1.9f + 1) * 22, 0 }, { 270, -150 + sinf(tm * 1.3f + 2) * 12, 0 },
        { -110, -185, 60 }, { 150, 60 + sinf(tm * 1.1f) * 20, -20 } };
    for (int i = 0; i < 5; i++) pg3d_blob_shadow(pos[i].x, FLOOR_Y + 1, pos[i].z, 80 + (pos[i].y + 200) * -0.08f + 20, 0.45f);

    PG_M4 m = pg_m4_mul(pg_m4_trs2(pos[0].x, pos[0].y, pos[0].z, sinf(tm * 0.9f) * 0.25f, 1, 1, 1), pg_m4_rot_y(sinf(tm * 0.7f) * 0.5f));
    pg3d_mesh(m_block, t_block, &m, pg_rgb(255, 255, 255), 0);
    m = pg_m4_mul(pg_m4_translate(pos[1].x, pos[1].y, pos[1].z), pg_m4_mul(pg_m4_rot_z(tm * 0.8f), pg_m4_rot_y(tm * 1.4f)));
    pg3d_mesh(m_ball, t_ball, &m, pg_rgb(255, 255, 255), 0);
    pg3d_shine(pg_v3(pos[1].x, pos[1].y, pos[1].z), 70, 0.9f);
    m = pg_m4_trs2(pos[2].x, pos[2].y, pos[2].z, sinf(tm * 1.2f) * 0.3f, 1, 1, 1);
    pg3d_mesh(m_cone, t_cone, &m, pg_rgb(255, 255, 255), 0);
    m = pg_m4_mul(pg_m4_translate(pos[3].x, pos[3].y, pos[3].z), pg_m4_rot_x(0.35f));
    pg3d_mesh(m_drum, t_drum, &m, pg_rgb(255, 255, 255), 0);
    m = pg_m4_mul(pg_m4_translate(pos[4].x, pos[4].y, pos[4].z), pg_m4_mul(pg_m4_rot_x(tm * 1.1f), pg_m4_rot_y(tm * 0.7f)));
    pg3d_mesh(m_ring, t_ring, &m, pg_rgb(255, 255, 255), 0);

    /* touch sparkles: stars are sprites, sorted with everything else; the glow is additive */
    for (int i = 0; i < MAX_SPARKS; i++) {
        const Spark *s = &sparks[i];
        if (s->life <= 0) continue;
        float k = s->life > 0.3f ? 1.0f : s->life / 0.3f;
        PG_Color c = pg_hsv(s->hue, 0.55f, 1.0f);
        pg3d_sprite(pg3d_builtin(PG3D_TEX_STAR), s->p, s->size * k, s->size * k, s->life * 4, c, 0);
        pg3d_sprite(pg3d_builtin(PG3D_TEX_GLOW), s->p, s->size * 2.2f * k, s->size * 2.2f * k, 0, pg_alpha(c, 0.6f), PG3D_ADDITIVE);
    }
    const PG_Touch *t = pg_touches();
    for (int i = 0; i < PG_MAX_TOUCH; i++) {
        PG_V3 w;
        if (t[i].state != PG_TOUCH_NONE && t[i].state != PG_TOUCH_UP && pg3d_plane_z(t[i].x, t[i].y, 0, &w)) {
            w.z = 80;
            pg3d_sprite(pg3d_builtin(PG3D_TEX_GLOW), w, 150, 150, 0, pg_alpha(pg_hsv(40.0f + 60.0f * (float)i, 0.4f, 1), 0.8f), PG3D_ADDITIVE);
        }
    }
    pg3d_end(r);
}

static void shutdown(void)
{
    pg_mesh_free(m_block); pg_mesh_free(m_ball); pg_mesh_free(m_cone); pg_mesh_free(m_drum);
    pg_mesh_free(m_ring); pg_mesh_free(m_wall); pg_mesh_free(m_floor);
}

static const PG_Game game = { "_template", "Template", init, update, render, shutdown };
int main(int argc, char **argv) { return pg_run(&game, argc, argv); }
