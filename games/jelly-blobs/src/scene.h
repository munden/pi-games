/* scene.h - the playroom, the camera and how a blob is drawn (lit pillow + face). */
#ifndef JB_SCENE_H
#define JB_SCENE_H

#include "art.h"
#include "soft.h"

/* Per-blob look and behaviour state (the physics lives in SoftBlob). */
typedef struct {
    int   style;                    /* colour / texture index */
    float gx, gy;                   /* smoothed gaze, -1..1 */
    float tx, ty, wander_t;         /* wander target (world) and time until the next one */
    float blink_t, blink_p;         /* time to next blink, blink progress 0..1 (0 = eyes open) */
    float hop_t, crouch_t;          /* time to next idle hop, crouch (anticipation) timer */
    int   mouth;                    /* FACE_SMILE / FACE_OH / FACE_GRIN */
    float mouth_t;                  /* minimum time before the mouth may change again */
    float giggle_t, poke_t, land_t; /* timers: giggling, just poked, just landed or bumped */
    float jx, jy, jvx, jvy;         /* the face jiggles a little inside the jelly (spring follower) */
    float acc_a, acc_v;             /* swing of the little leaf / antenna on top */
    float phase;                    /* breathing phase */
    float wide;                     /* eyes widen when startled, 0..1 */
    float snd_cool;                 /* per blob sound rate limit */
} BlobFx;

void  scene_init(const Art *art);
void  scene_shutdown(void);

void  scene_camera(float t, PG_Camera *cam);
bool  scene_unproject(float t, float sx, float sy, float *wx, float *wy);   /* canvas px -> z=0 plane */

void  scene_draw_room(float t);                       /* backdrop, drawn in submission order */
void  scene_draw_shadow(const SoftBlob *b);
void  scene_draw_blob(const SoftBlob *b, const BlobFx *fx, float t);

#endif
