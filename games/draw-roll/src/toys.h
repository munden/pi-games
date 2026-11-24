/* toys.h - Draw & Roll: the three big friendly things - the cloud dispenser, the bucket that eats balls, the sponge. */
#ifndef DR_TOYS_H
#define DR_TOYS_H

#include "util.h"

void toys_init(void);
void toys_shutdown(void);
void toys_update(void);
void toys_render_shadows(void);              /* BG list: soft shadows on the back wall */
void toys_render(void);

/* screen-space hit tests (canvas px) with generous padding */
bool toys_hit_dispenser(float sx, float sy);
bool toys_hit_sponge(float sx, float sy);

void toys_dispenser_mouth(float *x, float *y);   /* where a new ball appears */
void toys_dispenser_pop(void);                   /* squash, puff, "pop" */
void toys_bucket_eat(float x, float y);          /* gulp, chime, sparkles */
void toys_sponge_press(bool down);
void toys_sponge_wipe(void);
void toys_look_at(float wx, float wy);           /* the eyes follow this world point */

#endif
