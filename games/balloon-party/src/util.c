/* util.c - the camera, the balloon palette, texture upload. */
#include "bp.h"

/* One colour per pentatonic note: red is the low C, pink is the high E. */
const uint32_t BAL_RGB[N_COL] = {
    0xFF4658, 0xFF9A2E, 0xFFD635, 0x4CCB5E, 0x22C7C2, 0x3F86FF, 0xA566FF, 0xFF6FB6
};

PG_Color bal_color(int col, uint8_t a)
{
    uint32_t c = BAL_RGB[(unsigned)col % N_COL];
    return pg_rgba((uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c, a);
}

/* ------------------------------------------------------------------ camera
 * A slow dolly: the eye drifts sideways and a little up and down while it keeps looking straight at the
 * wall (target moves with the eye). That keeps the maths exact (a point at depth D lands at
 * 480 + (x-ex)*focal/D) and still gives real parallax between wall, balloons and floor. */
static Cam C;

void cam_update(float t)
{
    float ex = sinf(t * 0.27f) * 34.0f + sinf(t * 0.113f + 1.3f) * 14.0f;
    float ey = 14.0f + sinf(t * 0.19f + 0.6f) * 9.0f;
    PG_Camera cam = pg3d_camera_2d_fov(CAM_FOV);
    float dist = cam.eye.z;
    cam.eye = pg_v3(ex, ey, dist);
    cam.target = pg_v3(ex, ey, 0);
    C.pg = cam; C.ex = ex; C.ey = ey; C.dist = dist;
    C.focal = (PG_H * 0.5f) / tanf(CAM_FOV * 0.5f * PG_PI / 180.0f);
}

const Cam *cam_get(void) { return &C; }

float cam_scale(float z) { return C.focal / (C.dist - z); }

bool cam_project(float x, float y, float z, float *sx, float *sy)
{
    float d = C.dist - z;
    if (d < 30.0f) return false;
    float k = C.focal / d;
    *sx = PG_W * 0.5f + (x - C.ex) * k;
    *sy = PG_H * 0.5f - (y - C.ey) * k;
    return true;
}

void cam_unproject(float sx, float sy, float z, float *x, float *y)
{
    float k = (C.dist - z) / C.focal;
    *x = C.ex + (sx - PG_W * 0.5f) * k;
    *y = C.ey - (sy - PG_H * 0.5f) * k;
}

PG_Tex tex_upload(PG_Image *im, bool dither)
{
    if (dither) pg_img_ps1(im);
    PG_Tex t = pg3d_texture(im);
    pg_img_free(im);
    return t;
}
