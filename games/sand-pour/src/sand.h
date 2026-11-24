/* sand.h - the falling-sand cellular automaton of "Sand Pour".
 *
 * A byte-per-cell grid (SD_W x SD_H visible cells, 3x3 canvas px each) with a few hidden rows
 * below the floor.  Only chunks that changed recently are simulated, every move goes DOWN a row
 * (so there is never a double move), and the lit RGBA texture is rebuilt per changed chunk only.
 * No floats in the per-cell loop, no allocation after init.
 */
#ifndef SAND_H
#define SAND_H

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>

#define SD_W        228                 /* visible columns */
#define SD_H        120                 /* visible rows (the last SD_PLANK rows are the trapdoor floor) */
#define SD_CELL     3                   /* canvas px per cell */
#define SD_PLANK    3
#define SD_PX_W     (SD_W * SD_CELL)    /* 684 */
#define SD_PX_H     (SD_H * SD_CELL)    /* 360 */
#define SD_HUES     32
#define SD_GROUPS   8                   /* pile-height measuring columns */

void sand_init(SDL_Renderer *r, uint32_t seed);
void sand_shutdown(void);
void sand_reset(void);                              /* empty box, door closed (obstacles stay) */

void sand_step(void);                               /* one 60 Hz tick (also drives the trapdoor) */

/* Spawn up to n grains of colour `hue` (0..SD_HUES-1) along the segment (x0,y0)-(x1,y1), in CELL units
 * (x right 0..SD_W, y down 0..SD_H).  A scatter of +-2 cells makes a stream.  Returns grains created. */
int  sand_pour(float x0, float y0, float x1, float y1, int hue, int n);

/* trapdoor: the floor planks vanish from xc outward to radius r_target (cells), `rate` cells per tick,
 * stay open at least hold_min ticks and until the box is below close_fill (0..1), then close again. */
void sand_door_open(float xc, float r_target, float rate, int hold_min, float close_fill);
void sand_door_extend(void);                        /* keep an open door open a little longer */
int  sand_door_state(void);                         /* SAND_DOOR_* */
float sand_door_radius(void);                       /* cells */
float sand_door_center(void);                       /* cells */

enum { SAND_DOOR_CLOSED, SAND_DOOR_OPENING, SAND_DOOR_OPEN, SAND_DOOR_CLOSING };

float sand_fill(void);                              /* 0..1 of the free space */
int   sand_ceiling(void);                           /* settled grains in the top rows (box choking) */

/* Pile heights: call every few ticks.  heights[g] = tallest settled pile (in cells) in group g, top_x[g] its box column,
 * peak_x = the box column of the tallest pile overall. */
void  sand_scan(int heights[SD_GROUPS], int top_x[SD_GROUPS], int *peak_x);

/* a sample of the grains that left through the floor during the last sand_step: hue index and box column */
int   sand_drained(int *hues, int *cols, int max);

/* relight changed chunks, upload, and draw the window texture with its top-left at (dx,dy) canvas px */
void  sand_draw(SDL_Renderer *r, int dx, int dy);

/* box row of the first resting grain or obstacle at or below `from_row` in box column `col` (-1 if none): where a stream lands */
int   sand_land_row(int col, int from_row);

/* smoothed CPU ms per tick spent here (sim + relight): lets the game ease off pouring if the device struggles */
float sand_cpu_ms(void);

#endif
