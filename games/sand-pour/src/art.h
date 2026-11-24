/* art.h - every texture is painted at init (no files), every mesh is built at init.  Layout constants are shared. */
#ifndef ART_H
#define ART_H

#include "pg3d.h"
#include "sand.h"

/* ---- layout (canvas px at the neutral camera; world x = px - 480, world y = 270 - px) ---- */
#define WIN_X    214.0f                    /* top-left of the sand window */
#define WIN_Y    62.0f
#define CAM_FOV  44.0f
#define WX0      (WIN_X - 480.0f)
#define WX1      (WX0 + (float)SD_PX_W)
#define WYT      (270.0f - WIN_Y)
#define WYB      (WYT - (float)SD_PX_H)
#define ZF       44.0f                     /* frame depth in front of the sand plane */
#define ZB       44.0f                     /* box depth behind it */
#define BAR_T    28.0f                     /* side / top bar thickness */
#define BAR_B    46.0f                     /* bottom bar thickness (the trapdoor base) */
#define GROUND_Y (WYB - BAR_B)

typedef struct {
    PG_Tex wood_h, wood_v, post, wall, ground, sea, rays, trunk, leaf, coconut, btn, btnbase, crate, hand;
    PG_Tex stem, ball, bucket, sandcap, starfish, slot, spade, sheen, puff;
    PG_Tex sun[3], cloud[2], flower[3];
    PG_Mesh *bar_top, *bar_bot, *bar_l, *bar_r, *post_t, *post_b;
    PG_Mesh *panel, *tun_l, *tun_r, *tun_t, *tun_b;
    PG_Mesh *ground_tile, *sea_quad, *trunk_seg, *leaf_m, *coconut_m, *crate_m, *btn_cap, *btn_ring, *ball_m;
    PG_Mesh *bucket_m, *bucket_cap, *stem_m, *decal, *spade_h, *spade_b, *slot_m;
} Art;

extern Art A;

void art_init(void);
void art_shutdown(void);

#endif
