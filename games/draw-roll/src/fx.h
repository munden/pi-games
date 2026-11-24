/* fx.h - Draw & Roll: sparkles, confetti, soap bubbles and glow pulses (one small particle pool). */
#ifndef DR_FX_H
#define DR_FX_H

#include "util.h"

void fx_init(void);
void fx_shutdown(void);
void fx_update(float degrade);                 /* degrade 0..1: 1 = the device is struggling, spawn less */
void fx_render(void);

/* n twinkling stars flying out of (x,y,z); hue in degrees, hue_spread +-degrees, size in px */
void fx_burst(float x, float y, float z, int n, float speed, float hue, float hue_spread, float size);
void fx_confetti(float x, float y, float z, int n, float speed);
void fx_bubble(float x, float y, float z, float size, float rise);
void fx_glow(float x, float y, float z, float size, PG_Color c, float life);   /* additive pulse that swells and fades */
int  fx_count(void);

#endif
