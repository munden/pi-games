/* scene.h - Draw & Roll: the toy-theatre diorama: painted sky backdrop, soft cushion walls, grassy hills, trees.
 * The hills and walls are also the static physics (segments) the balls roll on and bounce off. */
#ifndef DR_SCENE_H
#define DR_SCENE_H

#include "util.h"

void scene_init(void);
void scene_shutdown(void);
void scene_physics(PG_World *w);         /* create the static bodies (hills, walls, ceiling) */
void scene_update(void);
void scene_render_back(void);            /* BG list: painted panel, sun, clouds, cushion walls */
void scene_render_front(void);           /* hills, trees, flowers (sorted list) */
void scene_wall_hit(int side, float impact);   /* 0 = left, 1 = right: the cushion bulges for a moment */
int  scene_wall_side(const PG_Body *wall);     /* which wall body is this (0/1), -1 if none */

#endif
