/* bt.h - Block Tower: shared declarations.
 *
 * All mutable state lives in ONE Game struct (a single static instance in main.c); the modules
 * (meshes, play, scene) receive a pointer to it. There are no other globals.
 *
 * World: pixels, +y up, +z toward the viewer. The physics plane is z = 0 and maps ~1:1 to the canvas.
 */
#ifndef BT_H
#define BT_H

#include "paint.h"
#include "pg3d.h"
#include "pg_phys.h"

#include <math.h>

/* ------------------------------------------------------------------ room */
#define WALL_X      440.0f            /* inner faces of the side walls */
#define FLOOR_Y    (-205.0f)          /* top of the floor */
#define CEIL_Y      236.0f            /* underside of the ceiling (just above the top of the screen) */
#define BACK_Z     (-290.0f)          /* the back wall */
#define PICK_Z       34.0f            /* plane where a finger meets the front faces of the blocks */
#define GRAVITY    (-1000.0f)

#define CHEST_HX     85.0f            /* the toy chest in the bottom-right corner: a solid platform */
#define CHEST_HY     70.0f            /* with the big "build again" button on its front */
#define CHEST_HZ     44.0f
#define CHEST_X     (WALL_X - CHEST_HX)
#define CHEST_Y     (FLOOR_Y + CHEST_HY)
#define CHEST_TOP   (FLOOR_Y + 2.0f * CHEST_HY)
#define BTN_R        54.0f            /* button radius in world px (diameter 108 + bezel > 110 px on screen) */
#define BTN_Y       (FLOOR_Y + 60.0f)
#define BTN_Z       (CHEST_HZ + 7.0f)

#define RAIN_V      470.0f            /* terminal speed of the raining blocks (px/s) */
#define RAIN_A     1900.0f
#define RAIN_TOP    (CEIL_Y + 150.0f)

#define MAX_BLOCKS   48               /* boxes + balls alive at once (BOX_CAP boxes) */
#define BOX_CAP      40
#define MAX_GHOSTS   64
#define MAX_PARTS   150
#define MAX_PLAN     64

/* ---------------------------------------------------------------- shapes */
enum { T_CUBE, T_SMALL, T_LONG, T_TALL, T_FLAT, T_TYPES };
enum { FAM_CUBE, FAM_LONG, FAM_TALL, FAM_FLAT, FAM_COUNT };
enum { KIND_BOX, KIND_BALL };
enum { TAG_STATIC = 1, TAG_PAD, TAG_BLOCK };

typedef struct { float hx, hy, hz, bevel; uint8_t family; uint8_t pitch; } ShapeDef;

static inline const ShapeDef *bt_shape(int type)
{
    static const ShapeDef S[T_TYPES] = {
        [T_CUBE]  = { 40, 40, 40, 8.0f, FAM_CUBE, 2 },     /* 80 x 80 */
        [T_SMALL] = { 28, 28, 28, 6.0f, FAM_CUBE, 4 },     /* 56 x 56 */
        [T_LONG]  = { 80, 30, 30, 8.0f, FAM_LONG, 0 },     /* 160 x 60 */
        [T_TALL]  = { 30, 60, 30, 8.0f, FAM_TALL, 1 },     /* 60 x 120 */
        [T_FLAT]  = { 60, 20, 28, 7.5f, FAM_FLAT, 3 },     /* 120 x 40 */
    };
    return &S[type];
}

static inline float bt_ball_r(int which) { return which ? 44.0f : 54.0f; }   /* the two big soft balls */
static inline float bt_clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float bt_lerpf(float a, float b, float t) { return a + (b - a) * t; }

/* a raining block accelerates to RAIN_V and then falls at that speed */
static inline float bt_fall_time(float d)
{
    float ta = RAIN_V / RAIN_A, sa = 0.5f * RAIN_A * ta * ta;
    return d <= sa ? sqrtf(2.0f * d / RAIN_A) : ta + (d - sa) / RAIN_V;
}
static inline float bt_fall_dist(float tau)
{
    float ta = RAIN_V / RAIN_A, sa = 0.5f * RAIN_A * ta * ta;
    return tau < ta ? 0.5f * RAIN_A * tau * tau : sa + RAIN_V * (tau - ta);
}

/* ----------------------------------------------------------------- state */
typedef struct Block {
    PG_Body *body;                    /* NULL: free slot */
    uint8_t kind, type, skin;         /* ball: skin selects the texture, type unused */
    uint8_t holders;                  /* fingers holding it */
    uint8_t squash_axis;              /* 0: squash along local y, 1: along local x */
    uint32_t birth;
    float squash, squash_v;           /* squash-and-stretch spring */
    float pop;                        /* spawn animation 0..1 */
    float lift;                       /* 0..1 smoothed "held" lift toward the viewer */
    float shade;                      /* tiny brightness variation per block */
    float snd_cd;                     /* per-block sound cooldown */
} Block;

enum { GH_NONE, GH_TIDY, GH_RAIN, GH_FADE };
typedef struct Ghost {                /* a block that is not a physics body right now: flying to the chest, raining in, fading */
    uint8_t mode, kind, type, skin;
    float shade;
    float t, dur;                     /* t < 0: waiting */
    float x0, y0, x1, y1, cx, cy;
    float ang, spin;
} Ghost;

enum { PT_NONE, PT_STAR, PT_DUST, PT_GLOW };
typedef struct Part {
    PG_V3 p, v;
    float life, max, size, grow, rot, spin, hue;
    uint8_t kind;
} Part;

enum { F_NONE, F_GRAB, F_PUSH, F_BUTTON, F_DEAD };
typedef struct Finger {
    uint8_t mode;
    Block *blk;                       /* F_GRAB */
    PG_Body *pad;                     /* F_PUSH: kinematic circle that shoves blocks */
    float lx, ly;                     /* grab point in the block's local frame */
    float wx, wy;                     /* finger in world px on the pick plane */
} Finger;

typedef struct Place { uint8_t kind, type, skin, level, group; float x, y; } Place;
typedef struct Plan { Place p[MAX_PLAN]; int n; } Plan;

enum { PH_PLAY, PH_TIDY, PH_RAIN };

typedef struct Textures {
    PG_Tex skin[FAM_COUNT][N_CUBE_SKINS];
    PG_Tex ball[2];
    PG_Tex floor, wall, wainscot, rug, view, chest, button, glove, fabric;
} Textures;

typedef struct Meshes {
    PG_Mesh *shape[T_TYPES];
    PG_Mesh *ball, *quad, *wall_tile, *back_tile, *wain_tile;
    PG_Mesh *floor_tile, *rug;
    PG_Mesh *chest_body, *chest_lid, *chest_plate, *btn_face, *btn_rim;
    PG_Mesh *baseboard, *rail, *side_base, *side_rail, *sill;
    PG_Mesh *frame_h, *frame_v, *bar_h, *bar_v, *rod, *shelf, *bracket;
    PG_Mesh *curtain, *mini;
    PG_Mesh *sph, *cone, *cyl, *torus;
    PG_Mesh *all[64];                 /* registry used by meshes_free() */
    int n;
} Meshes;

typedef struct Game {
    PG_World *w;
    PG_Body *chest;
    Block blocks[MAX_BLOCKS];
    Ghost ghosts[MAX_GHOSTS];
    Part parts[MAX_PARTS];
    Finger fingers[PG_MAX_TOUCH];
    Textures tex;
    Meshes mesh;
    Plan plan;

    int   phase;                      /* PH_PLAY / PH_TIDY / PH_RAIN */
    int   auto_rebuild;               /* the running rebuild was started by attract mode */
    uint32_t seq;                     /* birth counter: the oldest block fades first */
    int   bag[32], bag_n, bag_i;      /* shuffled deck of (type, skin) pairs for new blocks */
    float spawn_cd;                   /* global spawn cooldown */
    float snd_tokens;                 /* contact-sound budget */
    float idle_t;                     /* seconds since a finger last touched the screen */
    int   attract;                    /* 0 idle, 1 ball launched: wait for calm, then rebuild */
    float attract_t, calm_t;
    float giggle_t;                   /* idle "breathing": next block wobble */
    float press;                      /* button press animation 1..0 */
    float lid_kick;                   /* chest lid hop 1..0 */
    float tidy_note_cd;
    int   tidy_notes;
    int   hint_taps;                  /* hint-hand taps since the last real touch */
    float hand_vis, hand_x, hand_y, hand_tx, hand_ty, hand_t, hand_tap;
    int   hand_tapped;
    int   ghost_active;               /* ghosts alive (tidy/rain bookkeeping) */
    int   headless;                   /* software renderer: pg_frame_ms() says nothing about the Pi */
} Game;

/* meshes.c */
void meshes_build(Game *g);
void meshes_free(Game *g);

/* play.c */
void play_init(Game *g);
void play_shutdown(Game *g);
void play_update(Game *g);
int  play_box_count(const Game *g);

/* scene.c */
PG_Camera scene_camera(float t);
void scene_render(Game *g, SDL_Renderer *r);
void scene_textures(Game *g);

/* shared helpers implemented in play.c and used by the scene */
void fx_stars(Game *g, float x, float y, float z, int n, float speed, float size);
void fx_dust(Game *g, float x, float y, float z, int n, float size);

#endif
