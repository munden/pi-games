/* toys.c - Draw & Roll: the cloud dispenser, the smiling bucket and the sponge button.
 *
 * Dispenser: a puffy 3D cloud with a face; it bobs, blinks, squashes and puffs out a ball every ~2.5 s or on a tap.
 * Bucket:    a tapered pail with googly eyes that follow the balls; balls that fall into its mouth are eaten with a
 *            chime, a gulp and a burst of stars (the eating itself is decided in balls.c).
 * Sponge:    a big round button with a 3D sponge and floating soap bubbles; a tap washes every ramp away.
 */
#include "toys.h"
#include "balls.h"
#include "fx.h"

#include <string.h>

/* sponge button placement (world) */
#define SP_X  (-352.0f)
#define SP_Y  (-182.0f)
#define SP_Z    36.0f
#define SP_R    60.0f
/* bucket */
#define BK_BOT  (-250.0f)

static PG_Mesh *m_puff, *m_bucket, *m_rim, *m_inside, *m_eye, *m_ped, *m_pedrim, *m_sponge, *m_quad;
static PG_Tex   t_cloud, t_bucket, t_rim, t_inside, t_ped, t_sponge;
static PG_Tex   t_eye, t_pupil, t_cheek, t_smile, t_mouth;

static struct {
    float t;
    float disp_s, disp_v, disp_open, blink, next_blink;
    float gulp, gulp_v, happy;
    float press, press_v, wipe_t;
    bool  pressed;
    float look_x, look_y, look_hold, pupil_x, pupil_y;
    int   combo; uint64_t last_eat;
} T;

/* the cloud drifts slowly left and right so the balls land in different places */
static float disp_x(void) { return DISP_X + 85.0f * sinf(T.t * 0.36f); }

/* ---------------------------------------------------------------- painting */
static uint32_t rng_state = 0x1234567u;
static float rnd01(void)
{
    rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5;
    return (float)(rng_state >> 8) * (1.0f / 16777216.0f);
}

static void paint_face_sheet(void)
{
    PG_Image *im = pg_img_new(128, 64, 0x00FFFFFFu);
    /* eye white (0,0) */
    pg_img_disc(im, 16, 16, 14.8f, PG_RGB24(0xE6ECFA));
    pg_img_disc(im, 16, 14.5f, 13.4f, PG_RGB24(0xFFFFFF));
    /* pupil (32,0): deep violet with two sparkles */
    pg_img_disc(im, 48, 16, 14.0f, PG_RGB24(0x241F58));
    pg_img_disc(im, 48, 18, 10.0f, PG_RGB24(0x3A3394));
    pg_img_disc(im, 42.5f, 10.5f, 4.2f, PG_RGB24(0xFFFFFF));
    pg_img_disc(im, 54.5f, 21.5f, 2.0f, PG_RGB24(0xFFFFFF));
    /* cheek (64,0): soft pink glow */
    for (int j = 0; j < 32; j++)
        for (int i = 0; i < 32; i++) {
            float d = sqrtf(((float)i - 15.5f) * ((float)i - 15.5f) + ((float)j - 15.5f) * ((float)j - 15.5f)) / 16.0f;
            float a = d >= 1.0f ? 0.0f : (1.0f - d) * (1.0f - d);
            im->px[j * 128 + 64 + i] = PG_ARGB((uint32_t)(a * 215.0f), 255, 122, 156);
        }
    /* smile (0,32) 48x24 */
    for (int k = 0; k < 10; k++) {
        float t0 = (float)k / 10.0f, t1 = (float)(k + 1) / 10.0f;
        pg_img_line(im, 6.0f + 36.0f * t0, 32.0f + 5.0f + 12.0f * 4.0f * t0 * (1.0f - t0), 6.0f + 36.0f * t1, 32.0f + 5.0f + 12.0f * 4.0f * t1 * (1.0f - t1), 3.8f, PG_RGB24(0xC8325A));
    }
    /* open mouth (48,32) 32x32 with tongue */
    pg_img_disc(im, 64, 46, 12.6f, PG_RGB24(0x9A1E48));
    pg_img_disc(im, 64, 53, 7.2f, PG_RGB24(0xFF7C9C));
    /* flatten the top: a "D"-shaped mouth */
    for (int j = 32; j < 43; j++) for (int i = 48; i < 80; i++) im->px[j * 128 + i] = 0x00FFFFFFu;
    PG_Tex sheet = tex_finish(im, true);
    t_eye   = tex_sub(sheet, 128, 64, 0, 0, 32, 32);
    t_pupil = tex_sub(sheet, 128, 64, 32, 0, 32, 32);
    t_cheek = tex_sub(sheet, 128, 64, 64, 0, 32, 32);
    t_smile = tex_sub(sheet, 128, 64, 0, 32, 48, 24);
    t_mouth = tex_sub(sheet, 128, 64, 48, 32, 32, 32);
}

static PG_Tex paint_cloud(void)
{
    PG_Image *im = pg_img_new(32, 32, 0xFFFFFFFFu);
    pg_img_gradient_v(im, 0, 0, 32, 32, PG_RGB24(0xFFFFFF), PG_RGB24(0xE4EEFF));
    for (int i = 0; i < 9; i++) pg_img_disc(im, 3.0f + rnd01() * 26.0f, 3.0f + rnd01() * 26.0f, 2.0f + rnd01() * 4.0f, PG_ARGB(70, 200, 220, 255));
    pg_img_noise(im, 4, 9);
    return tex_finish(im, true);
}

static PG_Tex paint_bucket(void)
{
    PG_Image *im = pg_img_new(128, 64, 0xFFFFFFFFu);
    pg_img_gradient_v(im, 0, 0, 128, 64, PG_RGB24(0xFFA04A), PG_RGB24(0xEB6A34));
    for (int x = 4; x < 128; x += 16)                              /* toy-pail stripes, but keep the face area clean */
        if (x < 30 || x > 96) pg_img_rect(im, x, 0, 8, 64, PG_ARGB(46, 255, 244, 214));
    pg_img_rect(im, 0, 0, 128, 7, PG_RGB24(0xFFE7B8));             /* creamy top band under the rim */
    pg_img_rect(im, 0, 7, 128, 2, PG_RGB24(0xD9582C));
    pg_img_rect(im, 0, 56, 128, 8, PG_RGB24(0xC8502A));            /* darker foot */
    pg_img_rect(im, 0, 54, 128, 2, PG_RGB24(0xFFB777));
    pg_img_rect(im, 40, 9, 9, 45, PG_ARGB(36, 255, 255, 255));    /* soft vertical shine */
    pg_img_disc(im, 42.0f, 33.0f, 7.5f, PG_ARGB(165, 255, 108, 138));   /* rosy cheeks */
    pg_img_disc(im, 86.0f, 33.0f, 7.5f, PG_ARGB(165, 255, 108, 138));
    for (int k = 0; k < 12; k++) {                                 /* the smile */
        float t0 = (float)k / 12.0f, t1 = (float)(k + 1) / 12.0f;
        pg_img_line(im, 44.0f + 40.0f * t0, 36.0f + 13.0f * 4.0f * t0 * (1.0f - t0), 44.0f + 40.0f * t1, 36.0f + 13.0f * 4.0f * t1 * (1.0f - t1), 4.2f, PG_RGB24(0x7A1E38));
    }
    pg_img_noise(im, 4, 33);
    return tex_finish(im, true);
}

static PG_Tex paint_rim(void)
{
    PG_Image *im = pg_img_new(32, 16, 0xFFFFFFFFu);
    pg_img_stripes(im, 4, 0, PG_RGB24(0xFFFFFF), PG_RGB24(0x5EB6FF));
    return tex_finish(im, true);
}

static PG_Tex paint_inside(void)
{
    PG_Image *im = pg_img_new(32, 32, PG_RGB24(0x1B3358));
    pg_img_gradient_radial(im, 16, 16, 16, PG_RGB24(0x0F1D3C), PG_RGB24(0x2B6E86));
    pg_img_noise(im, 3, 14);
    return tex_finish(im, true);
}

static PG_Tex paint_pedestal(void)
{
    PG_Image *im = pg_img_new(32, 32, PG_RGB24(0x5AA6F5));
    pg_img_gradient_radial(im, 16, 16, 16, PG_RGB24(0xB8E2FF), PG_RGB24(0x4C94EE));
    pg_img_ring(im, 16, 16, 11.0f, 1.6f, PG_ARGB(200, 255, 255, 255));
    return tex_finish(im, true);
}

static PG_Tex paint_sponge(void)
{
    PG_Image *im = pg_img_new(64, 64, PG_RGB24(0xFFD848));
    for (int i = 0; i < 34; i++) pg_img_disc(im, rnd01() * 64.0f, rnd01() * 42.0f, 1.2f + rnd01() * 2.4f, PG_RGB24(0xE2A21E));
    for (int i = 0; i < 18; i++) pg_img_disc(im, rnd01() * 64.0f, rnd01() * 42.0f, 0.8f + rnd01() * 1.4f, PG_RGB24(0xFFF0A0));
    pg_img_rect(im, 0, 44, 64, 20, PG_RGB24(0x58C45C));              /* the scrubby green pad */
    for (int i = 0; i < 40; i++) pg_img_disc(im, rnd01() * 64.0f, 45.0f + rnd01() * 19.0f, 0.9f + rnd01() * 1.1f, PG_RGB24(0x3A9E48));
    pg_img_rect(im, 0, 43, 64, 2, PG_RGB24(0x3A8E42));
    pg_img_frame(im, 0, 0, 64, 64, 2, PG_ARGB(110, 255, 244, 170));
    return tex_finish(im, true);
}

/* tapered pail with an elliptical section: wide at the rim, narrower at the foot; front is at u = 0.5 */
static PG_Mesh *build_bucket(void)
{
    enum { N = 20 };
    const float y0 = BK_BOT, y1 = RIM_Y, rx0 = 88.0f, rz0 = 44.0f, rx1 = 114.0f, rz1 = 56.0f;
    const float drx = (rx1 - rx0) / (y1 - y0), drz = (rz1 - rz0) / (y1 - y0);
    MeshB b = mb_begin(2 * (N + 1), N * 6);
    for (int k = 0; k < 2; k++)
        for (int j = 0; j <= N; j++) {
            float a = PG_TAU * (float)j / (float)N, sa = sinf(a), ca = -cosf(a);
            float y = k ? y1 : y0, rx = k ? rx1 : rx0, rz = k ? rz1 : rz0;
            PG_V3 n = pg_v3_norm(pg_v3(2.0f * sa / rx, -2.0f * sa * sa * drx / rx - 2.0f * ca * ca * drz / rz, 2.0f * ca / rz));
            mb_vert(&b, pg_v3(rx * sa, y, rz * ca), n, (float)j / (float)N, k ? 0.0f : 1.0f, pg_rgb(255, 255, 255));
        }
    for (int j = 0; j < N; j++) mb_quad(&b, j, j + 1, (N + 1) + j + 1, (N + 1) + j);
    return mb_end(&b);
}

void toys_init(void)
{
    m_puff    = pg_mesh_sphere(1.0f, 10, 7);
    m_bucket  = build_bucket();
    m_rim     = pg_mesh_torus(115.0f, 11.0f, 18, 6);
    m_inside  = pg_mesh_cylinder(1.0f, 1.0f, 18);
    m_eye     = pg_mesh_sphere(1.0f, 10, 7);
    m_ped     = pg_mesh_cylinder(SP_R, 10.0f, 20);
    m_pedrim  = pg_mesh_torus(SP_R - 1.0f, 8.0f, 20, 6);
    m_sponge  = pg_mesh_bevel_box(44.0f, 28.0f, 22.0f, 10.0f);
    m_quad    = pg_mesh_quad(1.0f, 1.0f);
    t_cloud = paint_cloud(); t_bucket = paint_bucket(); t_rim = paint_rim();
    t_inside = paint_inside(); t_ped = paint_pedestal(); t_sponge = paint_sponge();
    paint_face_sheet();
    memset(&T, 0, sizeof T);
    T.next_blink = 2.0f;
}

void toys_shutdown(void)
{
    pg_mesh_free(m_puff); pg_mesh_free(m_bucket); pg_mesh_free(m_rim); pg_mesh_free(m_inside); pg_mesh_free(m_eye);
    pg_mesh_free(m_ped); pg_mesh_free(m_pedrim); pg_mesh_free(m_sponge); pg_mesh_free(m_quad);
    m_puff = m_bucket = m_rim = m_inside = m_eye = m_ped = m_pedrim = m_sponge = m_quad = NULL;
}

/* ------------------------------------------------------------------ logic */
static void spring(float *x, float *v, float target, float k, float c)
{
    *v += (k * (target - *x) - c * *v) * PG_DT;
    *x += *v * PG_DT;
}

void toys_dispenser_mouth(float *x, float *y)
{
    *x = disp_x();
    *y = DISP_Y - 60.0f + sinf(T.t * 1.9f) * 5.0f;
}

void toys_dispenser_pop(void)
{
    float mx, my;
    toys_dispenser_mouth(&mx, &my);
    T.disp_v = 14.0f; T.disp_open = 1.0f;
    fx_burst(mx, my, 4.0f, 6, 150.0f, 52.0f, 30.0f, 16.0f);
    fx_glow(mx, my - 6.0f, 20.0f, 150.0f, pg_rgb(255, 235, 170), 0.5f);
    pg_sfx_pop(0.34f);
}

void toys_bucket_eat(float x, float y)
{
    (void)y;
    uint64_t now = pg_ticks();
    T.combo = (T.last_eat != 0 && now - T.last_eat < 150) ? T.combo + 1 : 0;
    T.last_eat = now ? now : 1;
    T.gulp_v = 7.0f; T.happy = 0.9f;
    pg_sfx_chime(3 + (T.combo % 7), 0.55f);
    pg_sfx_sparkle(0.32f);
    float px = clampf(x, -MOUTH_X + 20.0f, MOUTH_X - 20.0f);
    fx_burst(px, RIM_Y + 14.0f, 56.0f, 9, 230.0f, 48.0f + (float)(T.combo % 7) * 42.0f, 50.0f, 22.0f);
    fx_confetti(px, RIM_Y + 10.0f, 52.0f, 6, 260.0f);
    fx_glow(px, RIM_Y + 6.0f, 50.0f, 240.0f, pg_rgb(255, 214, 130), 0.55f);
}

void toys_sponge_press(bool down)
{
    T.pressed = down;
    if (down) { T.press_v += 6.0f; pg_sfx_tap(0.45f); }
}

void toys_sponge_wipe(void)
{
    T.wipe_t = 1.0f;
    T.press_v += 5.0f;
    pg_sfx_whoosh(0.5f);
    for (int i = 0; i < 7; i++) fx_bubble(SP_X + pg_rand_range(-50.0f, 50.0f), SP_Y + pg_rand_range(0.0f, 50.0f), SP_Z + 20.0f, pg_rand_range(26.0f, 46.0f), pg_rand_range(70.0f, 130.0f));
}

void toys_look_at(float wx, float wy) { T.look_x = wx; T.look_y = wy; T.look_hold = 0.6f; }

void toys_update(void)
{
    T.t += PG_DT;
    spring(&T.disp_s, &T.disp_v, 0.0f, 190.0f, 11.0f);
    T.disp_open = decay(T.disp_open, 3.2f, PG_DT);
    T.next_blink -= PG_DT;
    if (T.next_blink <= 0.0f) { T.blink = 0.16f; T.next_blink = pg_rand_range(2.4f, 5.0f); }
    if (T.blink > 0.0f) T.blink -= PG_DT;
    spring(&T.gulp, &T.gulp_v, 0.0f, 170.0f, 9.0f);
    T.happy = fmaxf(0.0f, T.happy - PG_DT);
    spring(&T.press, &T.press_v, T.pressed ? 1.0f : 0.0f, 260.0f, 16.0f);
    if (T.wipe_t > 0.0f) T.wipe_t = fmaxf(0.0f, T.wipe_t - PG_DT);

    float tx = 0.0f, ty = -60.0f, bx, by;                           /* what the eyes look at: a finger, else the nearest ball */
    if (T.look_hold > 0.0f) { T.look_hold -= PG_DT; tx = T.look_x; ty = T.look_y; }
    else if (balls_nearest(0.0f, RIM_Y, &bx, &by)) { tx = bx; ty = by; }
    T.pupil_x = lerpf(T.pupil_x, tx, 0.12f); T.pupil_y = lerpf(T.pupil_y, ty, 0.12f);
}

/* ------------------------------------------------------------------ hit tests */
static bool hit_circle(float sx, float sy, PG_V3 c, float radius)
{
    float px, py;
    if (!pg3d_project(c, &px, &py, NULL)) return false;
    float dx = sx - px, dy = sy - py;
    return dx * dx + dy * dy <= radius * radius;
}

bool toys_hit_dispenser(float sx, float sy) { return hit_circle(sx, sy, pg_v3(disp_x(), DISP_Y + 4.0f, DISP_Z), 118.0f); }
bool toys_hit_sponge(float sx, float sy)    { return hit_circle(sx, sy, pg_v3(SP_X, SP_Y, SP_Z), SP_R * 1.06f + 18.0f); }

/* ------------------------------------------------------------------ drawing */
void toys_render_shadows(void)
{
    const unsigned fl = PG3D_BG | PG3D_UNLIT | PG3D_NOFOG;
    PG_Tex disc = pg3d_builtin(PG3D_TEX_SHADOW);
    float bob = sinf(T.t * 1.9f) * 5.0f;
    PG_M4 m = pg_m4_trs2(disp_x() * 1.06f + 18.0f, DISP_Y + bob - 30.0f, BACK_Z + 1.0f, 0, 150.0f, 74.0f, 1.0f);
    pg3d_mesh(m_quad, disc, &m, pg_rgba(255, 255, 255, 105), fl);
    m = pg_m4_trs2(30.0f, -218.0f, BACK_Z + 1.0f, 0, 190.0f, 82.0f, 1.0f);
    pg3d_mesh(m_quad, disc, &m, pg_rgba(255, 255, 255, 120), fl);
}

static void draw_cloud(void)
{
    static const struct { float x, y, z, r, ph; } puffs[] = {
        { -62, -10, -8, 42, 0.0f }, { 62, -10, -8, 42, 1.7f }, { -32, 36, -14, 38, 3.1f }, { 34, 38, -14, 36, 4.4f },
        { 0, -26, -6, 40, 2.3f },  { 0, 0, 0, 60, 0.7f } };
    float t = T.t, bob = sinf(t * 1.9f) * 5.0f;
    float sx = 1.0f + 0.12f * T.disp_s, sy = 1.0f - 0.18f * T.disp_s;
    PG_M4 G = pg_m4_mul(pg_m4_translate(disp_x(), DISP_Y + bob, DISP_Z), pg_m4_scale(sx, sy, 1.0f));
    for (int i = 0; i < 6; i++) {
        float wob = sinf(t * 2.3f + puffs[i].ph) * 2.2f;
        PG_M4 m = pg_m4_mul(G, pg_m4_trs2(puffs[i].x, puffs[i].y + wob, puffs[i].z, 0, puffs[i].r, puffs[i].r, puffs[i].r));
        pg3d_mesh(m_puff, t_cloud, &m, pg_rgb(255, 255, 255), 0);
    }
    /* the face: sprites just in front of the big puff */
    float fz = 62.0f;
    bool blink = T.blink > 0.0f;
    float happy = clampf(T.disp_open * 1.6f, 0.0f, 1.0f);
    float eye_h = blink ? 0.12f : 1.0f - 0.65f * happy;
    float lx = clampf((T.pupil_x - disp_x()) * 0.02f, -1.0f, 1.0f), ly = clampf((T.pupil_y - DISP_Y) * 0.02f, -1.0f, 1.0f);
    for (int s = -1; s <= 1; s += 2) {
        PG_V3 e = pg_m4_apply(&G, pg_v3(22.0f * (float)s, 10.0f, fz));
        pg3d_sprite(t_eye, e, 25.0f, 29.0f * eye_h, 0, pg_rgb(255, 255, 255), 0);
        if (eye_h > 0.5f) {
            PG_V3 p = pg_m4_apply(&G, pg_v3(22.0f * (float)s + lx * 4.0f, 10.0f + ly * 5.0f, fz + 1.5f));
            pg3d_sprite(t_pupil, p, 14.0f, 14.0f * (0.5f + 0.5f * eye_h), 0, pg_rgb(255, 255, 255), 0);
        }
        PG_V3 ch = pg_m4_apply(&G, pg_v3(41.0f * (float)s, -8.0f, fz - 3.0f));
        pg3d_sprite(t_cheek, ch, 26.0f, 22.0f, 0, pg_rgba(255, 255, 255, 235), 0);
    }
    PG_V3 mo = pg_m4_apply(&G, pg_v3(0.0f, -13.0f, fz));
    if (T.disp_open > 0.18f) pg3d_sprite(t_mouth, mo, 22.0f * (0.7f + 0.3f * T.disp_open), 18.0f * (0.6f + 0.4f * T.disp_open), 0, pg_rgb(255, 255, 255), 0);
    else                     pg3d_sprite(t_smile, mo, 28.0f, 14.0f, 0, pg_rgb(255, 255, 255), 0);
}

static void draw_bucket(void)
{
    float gs = T.gulp;
    float sx = 1.0f + 0.07f * gs, sy = 1.0f - 0.12f * gs;
    PG_M4 G = pg_m4_mul(pg_m4_translate(0.0f, BK_BOT, 0.0f), pg_m4_mul(pg_m4_scale(sx, sy, sx), pg_m4_translate(0.0f, -BK_BOT, 0.0f)));
    pg3d_mesh(m_bucket, t_bucket, &G, pg_rgb(255, 255, 255), 0);
    PG_M4 m = pg_m4_mul(G, pg_m4_trs2(0.0f, RIM_Y - 12.0f, 0.0f, 0, 109.0f, 1.5f, 53.0f));
    pg3d_mesh(m_inside, t_inside, &m, pg_rgb(255, 255, 255), 0);
    m = pg_m4_mul(G, pg_m4_trs2(0.0f, RIM_Y, 0.0f, 0, 1.0f, 1.0f, 0.5f));
    pg3d_mesh(m_rim, t_rim, &m, pg_rgb(255, 255, 255), 0);

    /* googly eyes that follow the nearest ball (or finger) */
    bool happy = T.happy > 0.0f;
    float blink = happy ? 0.14f : ((fmodf(T.t, 4.3f) < 0.13f) ? 0.14f : 1.0f);
    float dx = T.pupil_x, dy = T.pupil_y - (-199.0f);
    float dl = sqrtf(dx * dx + dy * dy) + 1e-3f, off = fminf(7.5f, dl * 0.05f);
    for (int s = -1; s <= 1; s += 2) {
        float ex = 40.0f * (float)s, ey = -199.0f, ez = 40.0f;
        m = pg_m4_mul(G, pg_m4_trs2(ex, ey, ez, 0, 19.0f, 19.0f * blink, 19.0f));
        pg3d_mesh(m_eye, t_eye, &m, pg_rgb(255, 255, 255), 0);
        if (blink > 0.5f) {
            float ox = (T.pupil_x - ex) / dl * off, oy = (T.pupil_y - ey) / dl * off;
            PG_V3 p = pg_m4_apply(&G, pg_v3(ex + ox, ey + oy, ez + 19.6f));
            pg3d_sprite(t_pupil, p, 15.5f, 15.5f, 0, pg_rgb(255, 255, 255), 0);
        }
    }
}

static void draw_sponge(void)
{
    float t = T.t, pr = clampf(T.press, -0.3f, 1.2f);
    PG_M4 G = pg_m4_mul(pg_m4_translate(SP_X, SP_Y, SP_Z), pg_m4_scale(1.0f - 0.08f * pr, 1.0f - 0.08f * pr, 1.0f - 0.45f * pr));
    PG_M4 face = pg_m4_rot_x(PG_PI * 0.5f);
    PG_M4 m = pg_m4_mul(G, face);
    pg3d_mesh(m_ped, t_ped, &m, pg_rgb(255, 255, 255), 0);
    m = pg_m4_mul(G, pg_m4_mul(pg_m4_translate(0.0f, 0.0f, 9.0f), face));
    pg3d_mesh(m_pedrim, t_rim, &m, pg_rgb(255, 255, 255), 0);

    float wob = sinf(t * 2.1f) * 0.07f, wipe = T.wipe_t;
    float sxo = wipe > 0.0f ? sinf(wipe * 22.0f) * 12.0f * smooth01(wipe * 3.0f) : 0.0f;
    float syo = wipe > 0.0f ? cosf(wipe * 22.0f) * 6.0f * smooth01(wipe * 3.0f) : 0.0f;
    PG_M4 rot = pg_m4_mul(pg_m4_rot_z(-0.20f + wob), pg_m4_mul(pg_m4_rot_x(0.52f), pg_m4_rot_y(0.46f + sinf(t * 1.3f) * 0.08f)));
    m = pg_m4_mul(G, pg_m4_mul(pg_m4_translate(sxo, 4.0f + syo + sinf(t * 2.6f) * 2.0f, 34.0f), rot));
    pg3d_mesh(m_sponge, t_sponge, &m, pg_rgb(255, 255, 255), 0);

    for (int i = 0; i < 4; i++) {                                   /* bubbles drift up and loop */
        float ph = fmodf(t * 0.35f + (float)i * 0.25f, 1.0f);
        float bx = SP_X + (-38.0f + 25.0f * (float)i) + sinf(t * 1.7f + (float)i * 2.0f) * 9.0f;
        float by = SP_Y + 12.0f + ph * 72.0f;
        float a = smooth01(ph * 5.0f) * smooth01((1.0f - ph) * 4.0f);
        float sz = 20.0f + 6.0f * (float)((i * 7) % 3);
        fx_draw_bubble(bx, by, SP_Z + 40.0f, sz * (0.5f + 0.5f * a), a);
    }
}

void toys_render(void)
{
    draw_cloud();
    draw_bucket();
    draw_sponge();
}
