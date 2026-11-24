/* Unit test of the evdev fallback parser (sdk/pg_evdev.c) with synthetic kernel events. It includes the source file
 * directly to reach its internals. Run by make test / make check. */
#include "../sdk/pg_evdev.c"
#include <assert.h>
#include <stdio.h>
void pg_log(const char *fmt, ...) { (void)fmt; }
static struct { int kind; int64_t id; float x, y; } got[64]; static int ng;
static void inj(int kind, int64_t id, float x, float y) { got[ng].kind = kind; got[ng].id = id; got[ng].x = x; got[ng].y = y; ng++; }
static void ev(Dev *d, int type, int code, int value) { struct input_event e; memset(&e, 0, sizeof e); e.type = (unsigned short)type; e.code = (unsigned short)code; e.value = value; process(d, &e, inj); }
static void syn(Dev *d) { ev(d, EV_SYN, SYN_REPORT, 0); }
int main(void)
{
    Dev d; memset(&d, 0, sizeof d); d.mt = true; d.index = 0; d.min_x = 0; d.max_x = 32767; d.min_y = 0; d.max_y = 32767;
    /* finger A in slot 0 */
    ev(&d, EV_ABS, ABS_MT_SLOT, 0); ev(&d, EV_ABS, ABS_MT_TRACKING_ID, 100); ev(&d, EV_ABS, ABS_MT_POSITION_X, 16383); ev(&d, EV_ABS, ABS_MT_POSITION_Y, 8191); syn(&d);
    assert(ng == 1 && got[0].kind == PG_INJECT_DOWN && got[0].id == (1 << 20) + 0 && got[0].x > 0.49f && got[0].x < 0.51f && got[0].y > 0.24f && got[0].y < 0.26f);
    /* finger B in slot 3 while A moves */
    ev(&d, EV_ABS, ABS_MT_SLOT, 3); ev(&d, EV_ABS, ABS_MT_TRACKING_ID, 101); ev(&d, EV_ABS, ABS_MT_POSITION_X, 32767); ev(&d, EV_ABS, ABS_MT_POSITION_Y, 0);
    ev(&d, EV_ABS, ABS_MT_SLOT, 0); ev(&d, EV_ABS, ABS_MT_POSITION_X, 24575); syn(&d);
    assert(ng == 3);
    assert(got[1].kind == PG_INJECT_MOVE && got[1].id == (1 << 20) + 0 && got[1].x > 0.74f && got[1].x < 0.76f);
    assert(got[2].kind == PG_INJECT_DOWN && got[2].id == (1 << 20) + 3 && got[2].x > 0.99f && got[2].y < 0.01f);
    /* nothing changed: no events */
    syn(&d); assert(ng == 3);
    /* A lifts */
    ev(&d, EV_ABS, ABS_MT_SLOT, 0); ev(&d, EV_ABS, ABS_MT_TRACKING_ID, -1); syn(&d);
    assert(ng == 4 && got[3].kind == PG_INJECT_UP && got[3].id == (1 << 20) + 0);
    /* a contact whose position arrives in a later report is not reported at (0,0) */
    ev(&d, EV_ABS, ABS_MT_SLOT, 5); ev(&d, EV_ABS, ABS_MT_TRACKING_ID, 102); syn(&d); assert(ng == 4);
    ev(&d, EV_ABS, ABS_MT_POSITION_X, 1000); ev(&d, EV_ABS, ABS_MT_POSITION_Y, 2000); syn(&d);
    assert(ng == 5 && got[4].kind == PG_INJECT_DOWN && got[4].id == (1 << 20) + 5);
    /* release_all lifts what is still down (B and the slot-5 contact) */
    release_all(&d, inj); assert(ng == 7 && got[5].kind == PG_INJECT_UP && got[6].kind == PG_INJECT_UP);
    /* single-touch digitiser: BTN_TOUCH + ABS_X/Y */
    Dev s; memset(&s, 0, sizeof s); s.mt = false; s.index = 1; s.max_x = 4095; s.max_y = 4095; ng = 0;
    ev(&s, EV_ABS, ABS_X, 2048); ev(&s, EV_ABS, ABS_Y, 1024); ev(&s, EV_KEY, BTN_TOUCH, 1); syn(&s);
    assert(ng == 1 && got[0].kind == PG_INJECT_DOWN && got[0].id == (2 << 20));
    ev(&s, EV_ABS, ABS_X, 4095); syn(&s); assert(ng == 2 && got[1].kind == PG_INJECT_MOVE && got[1].x > 0.99f);
    ev(&s, EV_KEY, BTN_TOUCH, 0); syn(&s); assert(ng == 3 && got[2].kind == PG_INJECT_UP);
    printf("evdev_test: OK\n");
    return 0;
}
