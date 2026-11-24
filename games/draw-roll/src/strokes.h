/* strokes.h - Draw & Roll: finger-drawn ramps. Each piece is a static physics segment AND a 3D tube. */
#ifndef DR_STROKES_H
#define DR_STROKES_H

#include "util.h"

#define PEN_HINT 16                          /* extra pen slot used by the hint hand (touches use 0..15) */
#define PEN_RAINBOW (-1)

void strokes_init(PG_World *w);
void strokes_shutdown(void);
void strokes_update(float degrade);          /* degrade 0..1 lowers the piece limit when the device struggles */
void strokes_render_shadows(void);           /* soft decals on the back wall (BG list) */
void strokes_render(void);                   /* tubes, joints, pen glows */

void strokes_pen_down(int pen, float x, float y, int color_idx);   /* color_idx: palette index or PEN_RAINBOW */
void strokes_pen_move(int pen, float x, float y);
void strokes_pen_up(int pen);
bool strokes_pen_active(int pen);
void strokes_pen_set_ttl(int pen, float seconds);                   /* this stroke fades away by itself after `seconds` */
int  strokes_next_color(void);                                      /* next palette index not used by another pen */
PG_Color strokes_palette(int idx);

void strokes_melt(int n);                    /* the n oldest pieces start to thin out and fade (idle clean-up) */
void strokes_wipe(void);                     /* wash everything away: debris + sparkles, bodies removed at once */
bool strokes_contact(PG_Body *seg);          /* a ball hit this piece: it lights up and bulges; true if it was not already glowing */
int  strokes_count(void);                    /* alive (not fading) pieces */
void strokes_add_line(const float *xy, int n, int color_idx);      /* instant polyline (used for the first ramp) */

#endif
