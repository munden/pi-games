/* pg3d.h - PS1-style low-poly 3D for the Pi Zero 2 W, on top of SDL_Renderer.
 *
 * The pipeline deliberately mirrors the original PlayStation:
 *   - vertices are transformed and lit on the CPU (Gouraud: lighting is per vertex),
 *   - textures are mapped affinely (no perspective correction - the classic warp),
 *   - there is no depth buffer: triangles are sorted back-to-front (painter's algorithm),
 *   - small chunky textures (<=128 px) painted procedurally, 15-bit dithered if you call pg_img_ps1().
 * Everything goes out through SDL_RenderGeometry() in a few large batches, so it runs on the
 * Pi's GLES2 renderer and on the software renderer used by --headless screenshots.
 *
 * "PS1 or better" is a floor, not a ceiling: more polygons, smooth lighting, soft shadows, particles
 * and sensible colour are encouraged - flat untextured 2D discs are not.
 *
 * World space: right-handed, +x right, +y UP, +z toward the viewer. pg3d_camera_2d() puts the
 * camera so that the z=0 plane maps 1:1 to canvas pixels with the origin at the canvas centre,
 * which is what the 2D physics engine (pg_phys.h) uses: draw physics bodies at z=0, give them depth
 * with z-thickness, and put a back wall at z<0 and a floor below.
 *
 * Budget (see AGENTS.md): about 6,000 triangles per frame typical, 10,000 peak, <=128px textures.
 *
 * Per frame:  pg3d_begin(&cam); pg3d_mesh(...) ... ; pg3d_end(r);
 * Triangles with the PG3D_BG flag are drawn first in submission order (floor, walls, decals,
 * shadows); everything else is depth sorted. Meshes should be convex-ish and not interpenetrate.
 */
#ifndef PG3D_H
#define PG3D_H

#include "pg.h"
#include <math.h>

/* ------------------------------------------------------------------- math */
typedef struct { float x, y, z; } PG_V3;
typedef struct { float m[16]; } PG_M4;                /* column-major: m[col*4 + row] */

static inline PG_V3 pg_v3(float x, float y, float z) { PG_V3 v = { x, y, z }; return v; }
static inline PG_V3 pg_v3_add(PG_V3 a, PG_V3 b)  { return pg_v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline PG_V3 pg_v3_sub(PG_V3 a, PG_V3 b)  { return pg_v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline PG_V3 pg_v3_scale(PG_V3 a, float k){ return pg_v3(a.x * k, a.y * k, a.z * k); }
static inline float pg_v3_dot(PG_V3 a, PG_V3 b)  { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline PG_V3 pg_v3_cross(PG_V3 a, PG_V3 b){ return pg_v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline float pg_v3_len(PG_V3 a)           { return sqrtf(pg_v3_dot(a, a)); }
static inline PG_V3 pg_v3_norm(PG_V3 a)          { float l = pg_v3_len(a); return l > 1e-8f ? pg_v3_scale(a, 1.0f / l) : pg_v3(0, 1, 0); }
static inline PG_V3 pg_v3_lerp(PG_V3 a, PG_V3 b, float t) { return pg_v3_add(a, pg_v3_scale(pg_v3_sub(b, a), t)); }

PG_M4 pg_m4_identity(void);
PG_M4 pg_m4_mul(PG_M4 a, PG_M4 b);                    /* a*b: b is applied first */
PG_M4 pg_m4_translate(float x, float y, float z);
PG_M4 pg_m4_scale(float x, float y, float z);
PG_M4 pg_m4_rot_x(float rad);
PG_M4 pg_m4_rot_y(float rad);
PG_M4 pg_m4_rot_z(float rad);
PG_M4 pg_m4_trs2(float x, float y, float z, float angle_z, float sx, float sy, float sz);  /* translate * rotZ * scale: the 2.5D body transform */
PG_V3 pg_m4_apply(const PG_M4 *m, PG_V3 p);

/* --------------------------------------------------------------- textures */
typedef struct { float u0, v0, u1, v1; } PG_Tex;      /* a rectangle of the shared atlas */
PG_Tex pg3d_texture(const PG_Image *img);             /* copy a painted image into the atlas (call in init) */
PG_Tex pg3d_white(void);                              /* solid white, for vertex-coloured geometry */
enum { PG3D_TEX_DISC, PG3D_TEX_SHADOW, PG3D_TEX_GLOW, PG3D_TEX_STAR, PG3D_TEX_COUNT };
PG_Tex pg3d_builtin(int which);                       /* soft disc, dark soft shadow disc, white glow, 5-point star */

/* ----------------------------------------------------------------- meshes */
typedef struct { float x, y, z, nx, ny, nz, u, v; PG_Color c; } PG_Vertex;   /* u,v in 0..1 over the texture */
typedef struct { PG_Vertex *v; uint16_t *idx; int nv, ni; float radius; } PG_Mesh;   /* ni = 3 per triangle, CCW from outside; radius = bounding sphere about the origin, filled on first draw */

PG_Mesh *pg_mesh_new(int nv, int ni);
void     pg_mesh_free(PG_Mesh *m);
PG_Mesh *pg_mesh_box(float hx, float hy, float hz);                           /* half extents; texture on every face */
PG_Mesh *pg_mesh_bevel_box(float hx, float hy, float hz, float bevel);        /* chamfered edges: the toy-block look */
PG_Mesh *pg_mesh_sphere(float radius, int segments, int rings);               /* 10x7 is plenty; seam at the back */
PG_Mesh *pg_mesh_cylinder(float radius, float half_height, int segments);     /* axis = y, capped */
PG_Mesh *pg_mesh_cone(float radius, float half_height, int segments);         /* axis = y, apex up */
PG_Mesh *pg_mesh_torus(float major_r, float minor_r, int segments, int sides);
PG_Mesh *pg_mesh_quad(float half_w, float half_h);                            /* XY plane, faces +z */
PG_Mesh *pg_mesh_floor(float half_x, float half_z, int div_x, int div_z);     /* XZ plane, faces +y; subdivide so near clipping is kind */
void     pg_mesh_tint(PG_Mesh *m, PG_Color c);                                /* multiply vertex colours */
void     pg_mesh_gradient_y(PG_Mesh *m, PG_Color bottom, PG_Color top);        /* bake a vertical colour ramp into the vertices */
PG_Mesh *pg_mesh_build(const PG_Vertex *verts, int nv, const uint16_t *idx, int ni);   /* copy your own geometry */

/* ----------------------------------------------------------------- camera */
typedef struct { PG_V3 eye, target, up; float fov_deg, near, far; } PG_Camera;
PG_Camera pg3d_camera_2d(void);        /* z=0 plane == canvas pixels (origin at centre, y up); fov 40, mild perspective */
PG_Camera pg3d_camera_2d_fov(float fov_deg);   /* same mapping for z=0 with a stronger/weaker perspective */

/* ------------------------------------------------------------- scene state */
void pg3d_begin(const PG_Camera *cam);                 /* start a frame (resets the triangle lists) */
void pg3d_sky(PG_Color top, PG_Color bottom);          /* vertical gradient drawn behind everything (persists) */
void pg3d_light(PG_V3 dir_to_light, PG_Color key, PG_Color sky_ambient, PG_Color ground_ambient);   /* persists */
void pg3d_fog(PG_Color color, float start, float end); /* depth cue, PS1 style; end<=start disables (persists) */

enum {
    PG3D_BG       = 1,    /* draw before sorted geometry, in submission order (floor, walls, shadows, decals) */
    PG3D_UNLIT    = 2,    /* skip lighting: vertex colour * tint only */
    PG3D_TWOSIDED = 4,    /* no back-face culling (lighting uses |N.L|) */
    PG3D_ADDITIVE = 8,    /* additive blend, drawn last, not occluded (sparkles, glows) */
    PG3D_NOFOG    = 16
};

void pg3d_mesh(const PG_Mesh *m, PG_Tex tex, const PG_M4 *model, PG_Color tint, unsigned flags);
void pg3d_tri(PG_Tex tex, const PG_Vertex *a, const PG_Vertex *b, const PG_Vertex *c, unsigned flags);   /* world-space, for dynamic geometry */
void pg3d_sprite(PG_Tex tex, PG_V3 pos, float w, float h, float roll_rad, PG_Color tint, unsigned flags); /* camera-facing quad */
void pg3d_blob_shadow(float x, float floor_y, float z, float radius, float alpha);   /* soft dark disc lying on a floor */
void pg3d_shine(PG_V3 sphere_center, float radius, float strength);                  /* glossy highlight on a ball (0..1) */
void pg3d_end(SDL_Renderer *r);                        /* sort + draw (sky first) */
void pg3d_flush(SDL_Renderer *r);                      /* draw what is queued so far, keep the camera: lets you interleave 2D draws */

/* ---------------------------------------------------------------- picking */
bool pg3d_project(PG_V3 p, float *sx, float *sy, float *depth);       /* world -> canvas px; false if behind the camera */
void pg3d_ray(float sx, float sy, PG_V3 *origin, PG_V3 *dir);         /* canvas px -> world ray */
bool pg3d_plane_z(float sx, float sy, float z, PG_V3 *out);           /* canvas px -> point on the plane z=const (touch -> world) */
int  pg3d_last_tris(void);                                            /* triangles drawn last frame (watch the budget) */
float pg3d_cpu_ms(void);                                              /* CPU ms spent in pg3d this frame (transform, light, sort), not GPU/raster */

#endif /* PG3D_H */
