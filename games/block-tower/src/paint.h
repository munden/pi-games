/* paint.h - Block Tower procedural textures. Pure CPU painting (no SDL): every function returns a
 * freshly painted PG_Image that the caller uploads with pg3d_texture() and frees. */
#ifndef BT_PAINT_H
#define BT_PAINT_H

#include "pg.h"

enum { N_CUBE_SKINS = 8, N_LONG_SKINS = 2, N_TALL_SKINS = 3, N_FLAT_SKINS = 1 };

/* toy blocks: the rim band of each face lands on the chamfer of the block mesh */
PG_Image *paint_cube(int skin);       /* 64x64: letters, numbers, a star, animal faces */
PG_Image *paint_long(int skin);       /* 128x48 */
PG_Image *paint_tall(int skin);       /* 48x96 */
PG_Image *paint_flat(int skin);       /* 96x32 */
PG_Image *paint_ball(int which);      /* 64x64 sphere skins: 0 beach ball, 1 star ball */

/* the toy room */
PG_Image *paint_floor(void);          /* 128x64: two planks, tiled by geometry */
PG_Image *paint_wallpaper(void);      /* 128x128 */
PG_Image *paint_wainscot(void);       /* 64x64 cream painted boards */
PG_Image *paint_rug(void);            /* 128x128, round */
PG_Image *paint_view(void);           /* 96x96 window view: sky, sun, hills */
PG_Image *paint_chest_front(void);    /* 96x80 */
PG_Image *paint_button(void);         /* 64x64 round pictogram: a little block pyramid and a star */
PG_Image *paint_glove(void);          /* 64x64 pointing glove, finger up */
PG_Image *paint_fabric(void);         /* 16x16 neutral plush/paint grain to be tinted */
PG_Image *paint_poster(void);         /* 64x80 painting: a hot-air balloon over hills */

#endif
