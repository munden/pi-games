/* Unit tests for sdk/pg_phys.c. No SDL. Run by `make test` / `make check`. */
#include "pg_phys.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
#define DT (1.0f / 60.0f)

static void run(PG_World *w, int ticks) { for (int i = 0; i < ticks; i++) pg_world_step(w, DT); }
static float frand(void) { return (float)rand() / (float)RAND_MAX; }

static void test_bounce(void)
{
    printf("bounce\n");
    PG_World *w = pg_world_new(0, -900);
    pg_world_add_box(w, 0, -50, 1000, 50, 0, 0);
    PG_Body *ball = pg_world_add_circle(w, 0, 400, 20, 1);
    ball->restitution = 0.6f;
    float apex = 0; bool hit = false, rising = false;
    for (int i = 0; i < 60 * 3; i++) {
        float vy0 = ball->vy;
        pg_world_step(w, DT);
        if (!hit && ball->y < 22) hit = true;
        if (hit && ball->vy > 0 && vy0 <= 0) rising = true;
        if (hit && rising && ball->y > apex) apex = ball->y;
    }
    printf("  first rebound apex = %.1f (drop 380, e=0.6 -> ~137)\n", apex - 20);
    CHECK(apex - 20 > 80 && apex - 20 < 190, "rebound height %.1f out of range", apex - 20);
    run(w, 60 * 15);
    CHECK(fabsf(ball->vy) < 6, "ball still moving: vy=%.2f", ball->vy);
    CHECK(fabsf(ball->y - 20) < 2.0f, "ball not resting on floor: y=%.2f", ball->y);
    pg_world_free(w);
}

static void test_tower(void)
{
    printf("tower\n");
    PG_World *w = pg_world_new(0, -900);
    pg_world_add_box(w, 0, -50, 1000, 50, 0, 0);
    PG_Body *top = NULL; float y0 = 0;
    for (int i = 0; i < 8; i++) { top = pg_world_add_box(w, 0, 20 + i * 40 + 0.5f, 30, 20, 0, 1); y0 = top->y; }
    run(w, 60 * 10);
    printf("  top moved dx=%.2f dy=%.2f angle=%.3f\n", top->x, top->y - y0, top->angle);
    CHECK(fabsf(top->x) < 2.5f, "tower drifted sideways: %.2f", top->x);
    CHECK(fabsf(top->y - y0) < 6.0f, "tower sank/rose: %.2f", top->y - y0);
    CHECK(fabsf(top->angle) < 0.05f, "tower tilted: %.3f", top->angle);
    pg_world_free(w);
}

static void test_pyramid(void)
{
    printf("pyramid\n");
    PG_World *w = pg_world_new(0, -900);
    pg_world_add_box(w, 0, -50, 1000, 50, 0, 0);
    PG_Body *list[40]; float ox[40], oy[40]; int n = 0;
    for (int r = 0; r < 6; r++)
        for (int i = 0; i < 6 - r; i++) {
            float x = ((float)i - (5 - r) * 0.5f) * 51.0f, y = 20 + (float)r * 40.5f;
            list[n] = pg_world_add_box(w, x, y, 25, 20, 0, 1); ox[n] = x; oy[n] = y; n++;
        }
    run(w, 60 * 8);
    float worst = 0;
    for (int i = 0; i < n; i++) { float d = hypotf(list[i]->x - ox[i], list[i]->y - oy[i]); if (d > worst) worst = d; }
    printf("  %d boxes, worst displacement %.2f px\n", n, worst);
    CHECK(worst < 10.0f, "pyramid collapsed: worst displacement %.2f", worst);
    pg_world_free(w);
}

static void test_pile(void)
{
    printf("pile\n");
    PG_World *w = pg_world_new(0, -900);
    pg_world_add_box(w, 0, -50, 400, 50, 0, 0);
    pg_world_add_box(w, -350, 300, 50, 400, 0, 0);
    pg_world_add_box(w, 350, 300, 50, 400, 0, 0);
    srand(5);
    PG_Body *balls[120]; int n = 0;
    for (int i = 0; i < 120; i++) { balls[n] = pg_world_add_circle(w, -250 + frand() * 500, 100 + frand() * 600, 14, 1); balls[n]->restitution = 0.4f; n++; }
    run(w, 60 * 10);
    float sum = 0; int outside = 0;
    for (int i = 0; i < n; i++) { sum += pg_body_speed(balls[i]); if (balls[i]->x < -310 || balls[i]->x > 310 || balls[i]->y < 0) outside++; }
    printf("  mean speed %.2f px/s, outside=%d\n", sum / (float)n, outside);
    CHECK(outside == 0, "%d balls escaped the container", outside);
    CHECK(sum / (float)n < 20.0f, "pile still jittering: mean speed %.2f", sum / (float)n);
    pg_world_free(w);
}

static void test_ramp(void)
{
    printf("ramp\n");
    PG_World *w = pg_world_new(0, -900);
    pg_world_add_segment(w, -300, 200, 300, 0, 7);
    PG_Body *b = pg_world_add_circle(w, -250, 260, 15, 1);
    float min_clear = 1e9f;
    for (int i = 0; i < 60 * 3; i++) {
        pg_world_step(w, DT);
        float abx = 600, aby = -200, t = ((b->x + 300) * abx + (b->y - 200) * aby) / (abx * abx + aby * aby);
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        float d = hypotf(b->x - (-300 + abx * t), b->y - (200 + aby * t)) - 15 - 7;
        if (d < min_clear) min_clear = d;
    }
    printf("  rolled to x=%.1f, min clearance %.2f\n", b->x, min_clear);
    CHECK(b->x > -80, "ball did not roll down the ramp: x=%.1f", b->x);
    CHECK(min_clear > -4.0f, "ball tunnelled / sank into the ramp: clearance %.2f", min_clear);
    pg_world_free(w);
}

static void test_kinematic(void)
{
    printf("kinematic\n");
    PG_World *w = pg_world_new(0, -900);
    pg_world_add_box(w, 0, -50, 1000, 50, 0, 0);
    PG_Body *ball = pg_world_add_circle(w, 100, 20, 20, 1);
    PG_Body *finger = pg_world_add_circle(w, -100, 30, 30, 0);
    for (int i = 0; i < 60; i++) { finger->vx = 400; pg_world_step(w, DT); }
    printf("  ball pushed to x=%.1f\n", ball->x);
    CHECK(ball->x > 180, "kinematic body did not push the ball: x=%.1f", ball->x);
    CHECK(finger->x > 200, "kinematic body was blocked: x=%.1f", finger->x);
    pg_world_free(w);
}

static void test_grab(void)
{
    printf("grab\n");
    PG_World *w = pg_world_new(0, -900);
    pg_world_add_box(w, 0, -50, 1000, 50, 0, 0);
    PG_Body *box = pg_world_add_box(w, 0, 25, 40, 25, 0, 1);
    float lx, ly; pg_body_world_to_local(box, 10, 30, &lx, &ly);
    for (int i = 0; i < 120; i++) { pg_body_grab_step(box, lx, ly, 250, 180, 5.0f, 0.8f, DT); pg_world_step(w, DT); }
    float wx, wy; pg_body_local_to_world(box, lx, ly, &wx, &wy);
    printf("  grab point at %.1f,%.1f (target 250,180)\n", wx, wy);
    CHECK(hypotf(wx - 250, wy - 180) < 30, "grab spring did not hold the block near the finger");
    pg_world_free(w);
}

static int impacts;
static void on_hit(PG_Body *a, PG_Body *b, float speed, float x, float y, void *ud) { (void)a; (void)b; (void)x; (void)y; (void)ud; if (speed > 100) impacts++; }
static void test_events(void)
{
    printf("events\n");
    PG_World *w = pg_world_new(0, -900);
    pg_world_set_contact_fn(w, on_hit, NULL);
    pg_world_add_box(w, 0, -50, 1000, 50, 0, 0);
    PG_Body *b = pg_world_add_circle(w, 0, 300, 20, 1); b->restitution = 0.7f;
    run(w, 60 * 4);
    printf("  impacts=%d\n", impacts);
    CHECK(impacts >= 2 && impacts <= 12, "unexpected impact event count %d", impacts);
    pg_world_free(w);
}

static void test_perf(void)
{
    printf("perf\n");
    PG_World *w = pg_world_new(0, -900);
    pg_world_add_box(w, 0, -50, 600, 50, 0, 0);
    pg_world_add_box(w, -500, 300, 50, 400, 0, 0);
    pg_world_add_box(w, 500, 300, 50, 400, 0, 0);
    srand(9);
    for (int i = 0; i < 250; i++) pg_world_add_circle(w, -400 + frand() * 800, 100 + frand() * 900, 16, 1);
    run(w, 120);
    clock_t t0 = clock();
    run(w, 300);
    double ms = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC / 300.0;
    printf("  250 circles: %.2f ms per tick on this machine (Pi Zero 2 W is several times slower)\n", ms);
    CHECK(ms < 12.0, "physics too slow: %.2f ms/tick", ms);
    pg_world_free(w);
}

int main(void)
{
    test_bounce(); test_tower(); test_pyramid(); test_pile(); test_ramp(); test_kinematic(); test_grab(); test_events(); test_perf();
    if (fails) { printf("phys_test: %d FAILED\n", fails); return 1; }
    printf("phys_test: OK\n");
    return 0;
}
