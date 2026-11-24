/* main.c - Block Tower: chunky wooden toy blocks to stack, topple and fling in a cosy toy room.
 *
 * Touch a block to pick it up (ten fingers, ten blocks); tap the air and a new block drops from the sky;
 * sweep a finger through a tower to knock it over; press the big button on the toy chest and everything
 * is tidied into the chest while a fresh set of towers rains down. Left alone, a ball rolls in and knocks
 * a tower over, then the room tidies itself and builds again. */
#include "bt.h"

static Game G;

static int init(SDL_Renderer *r)
{
    (void)r;
    scene_textures(&G);
    meshes_build(&G);
    PG_Camera cam = scene_camera(0);
    pg3d_begin(&cam);                 /* so touches map correctly before the first frame is drawn */
    play_init(&G);
    return 0;
}

static void update(void) { play_update(&G); }
static void render(SDL_Renderer *r) { scene_render(&G, r); }

static void shutdown(void)
{
    play_shutdown(&G);
    meshes_free(&G);
}

static const PG_Game game = { "block-tower", "Block Tower", init, update, render, shutdown };
int main(int argc, char **argv) { return pg_run(&game, argc, argv); }
