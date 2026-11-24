/* art.h - every texture of Jelly Blobs, painted procedurally at init (no files). */
#ifndef JB_ART_H
#define JB_ART_H

#include "pg3d.h"

#define JB_STYLES 6

typedef struct {
    PG_Tex jelly[JB_STYLES];          /* glossy candy bodies */
    PG_Tex face;                      /* 4x2 sheet: sclera, pupil, blink, happy arc / smile, oh, grin, blush */
    PG_Tex fx;                        /* 4x2 sheet: bean highlight, dot highlight, heart, sparkle / puff, shadow, balloon, cloud */
    PG_Tex hand;                      /* pointing glove */
    PG_Tex wall, wain, ceil_t, floor_t, wood, sky, flag, fur;
    PG_Tex block[3], ball;
} Art;

enum { FACE_SCLERA, FACE_PUPIL, FACE_BLINK, FACE_HAPPY, FACE_SMILE, FACE_OH, FACE_GRIN, FACE_BLUSH };
enum { FX_BEAN, FX_DOT, FX_HEART, FX_SPARKLE, FX_PUFF, FX_SHADOW, FX_BALLOON, FX_CLOUD };

void art_init(Art *a);
/* a cell of a sheet (cols x rows), shrunk by a hair so nearest sampling never touches the neighbour */
PG_Tex art_cell(PG_Tex sheet, int idx, int cols, int rows);
PG_Color art_candy(int style);        /* the body colour of a style */

#endif
