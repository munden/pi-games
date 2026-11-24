/* TEMP harness: sim only, to tune the feel before the scene exists. */
#include "pg3d.h"
#include "sand.h"

#define WIN_X 214
#define WIN_Y 62

static float fhue[PG_MAX_TOUCH], idle_hue;
static float last_x[PG_MAX_TOUCH], last_y[PG_MAX_TOUCH];

static int init(SDL_Renderer *r)
{
    sand_init(r, pg_rand());
    pg3d_sky(pg_hex(0xBFE6FF), pg_hex(0xFFF1D0));
    return 0;
}

static void update(void)
{
    const PG_Touch *t = pg_touches();
    for (int i = 0; i < PG_MAX_TOUCH; i++) {
        if (t[i].state == PG_TOUCH_NONE || t[i].state == PG_TOUCH_UP) continue;
        float cx = (t[i].x - WIN_X) / SD_CELL, cy = (t[i].y - WIN_Y + 8) / SD_CELL;
        if (t[i].state == PG_TOUCH_DOWN) { fhue[i] = (float)(i * 53 % 360); last_x[i] = cx; last_y[i] = cy; }
        sand_pour(last_x[i], last_y[i], cx, cy, (int)(fhue[i] / (360.0f / SD_HUES)), 4);
        last_x[i] = cx; last_y[i] = cy;
        fhue[i] += 0.5f;
    }
    idle_hue += 0.9f;
    if (pg_ticks() % 2 == 0) sand_pour(SD_W / 2, 7, SD_W / 2, 8, (int)(idle_hue / (360.0f / SD_HUES)), 2);
    sand_step();
}

static void render(SDL_Renderer *r)
{
    PG_Camera cam = pg3d_camera_2d();
    pg3d_begin(&cam);
    pg3d_flush(r);
    sand_draw(r, WIN_X, WIN_Y);
    pg3d_end(r);
}

static void shutdown(void) { sand_shutdown(); }

static const PG_Game game = { "sand-pour", "Sand Pour", init, update, render, shutdown };
int main(int argc, char **argv) { return pg_run(&game, argc, argv); }
