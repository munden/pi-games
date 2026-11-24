/* balls.h - Draw & Roll: the shiny painted balls. Dropped by the cloud, rolled by the ramps, eaten by the bucket. */
#ifndef DR_BALLS_H
#define DR_BALLS_H

#include "util.h"

void balls_init(PG_World *w);
void balls_shutdown(void);
void balls_update(float degrade);                /* auto-drop timer, eating, recycling, squash */
bool balls_drop(void);                           /* a new ball from the cloud (also used by taps); false if too soon */
void balls_contact(PG_Body *ball, float impact, float x, float y);   /* squash along the contact normal */
void balls_render_shadows(void);
void balls_render(void);
bool balls_nearest(float x, float y, float *bx, float *by);          /* nearest live ball, for the toys' eyes */

#endif
