/* pit.h - shared declarations for the Ball Pit game (layout constants, texture set, mesh builders). */
#ifndef PIT_H
#define PIT_H

#include "pg3d.h"
#include "pg_phys.h"

/* ---- the tub, in world units. z = 0 is the plane the physics lives in (1 unit ~ 1 canvas px there). ---- */
#define WALL_X     410.0f     /* inner face of the two side walls */
#define FLOOR_Y   (-205.0f)   /* top of the tub floor */
#define CEIL_Y     250.0f     /* underside of the top beam: balls bounce off it */
#define BACK_Z    (-140.0f)   /* back wall of the playroom */
#define LIP_Z0     56.0f      /* the front lip: back edge of its top face ... */
#define LIP_Z1     100.0f     /* ... and its front face */
#define LIP_TOP   (-172.0f)

/* ---- painted textures ---- */
enum {
    BT_PLAIN, BT_BEACH, BT_STRIPES, BT_DOTS_RED, BT_DOTS_BLUE, BT_STARS_BLUE, BT_STARS_PURPLE,
    BT_HEARTS, BT_DAISY, BT_SWIRL_RED, BT_SWIRL_GREEN, BT_CHECK, BT_SMILEY, BT_COUNT
};

typedef struct {
    PG_Tex ball[BT_COUNT];
    PG_Tex wallpaper, quilt, wood, lip, mat, rainbow, cloud, sun, flag, hand, ring;
} PitTex;

void paint_textures(PitTex *t);                     /* paint.c: everything is painted at init, no files */

/* ---- custom meshes (build.c) ---- */
/* A grid of nx*ny quads spanning origin + i*du + j*dv; EVERY quad maps the whole texture 0..1 (tiling). */
PG_Mesh *mesh_tiles(PG_V3 origin, PG_V3 du, PG_V3 dv, int nx, int ny, PG_V3 normal, bool flip_v);
/* Half a rainbow: the front half of a tube bent into a half circle in the XY plane (u along, v across). */
PG_Mesh *mesh_arch(float radius, float tube, int segs, int sides);
/* One bunting flag: a double-sided triangle hanging from y=0. */
PG_Mesh *mesh_pennant(float width, float height);

#endif
