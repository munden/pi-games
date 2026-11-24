/* pg_evdev.c - fallback multitouch reader straight from the Linux input layer.
 *
 * SDL2's KMSDRM backend normally delivers touch through its own evdev code. If SDL reports no touch device
 * (an IR frame that enumerates oddly, a udev quirk) the runtime reads /dev/input/event* itself and feeds the same
 * touch pipeline. It understands multitouch protocol B (what Linux's hid-multitouch produces for USB HID frames:
 * ABS_MT_SLOT / TRACKING_ID / POSITION_X/Y) and plain single-touch digitisers (ABS_X/ABS_Y + BTN_TOUCH). It stops as
 * soon as SDL finds a touch device so a contact is never reported twice. Needs read access to /dev/input (group 'input').
 */
#include "pg_internal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define MAX_DEV   4
#define MAX_SLOT  16

typedef struct {
    bool  active, reported, have_x, have_y, moved;
    float x, y;                                   /* normalised 0..1 */
} Slot;

typedef struct {
    int   fd, index;
    bool  mt;
    int   min_x, max_x, min_y, max_y;
    int   cur;
    Slot  s[MAX_SLOT];
    char  name[64];
} Dev;

static Dev      devs[MAX_DEV];
static int      ndev;
static uint32_t last_scan_ms;

static float norm(int v, int lo, int hi) { float n = hi > lo ? (float)(v - lo) / (float)(hi - lo) : 0.0f; return n < 0 ? 0 : n > 1 ? 1 : n; }

/* Feed one kernel event. Separate from the fd handling so it can be tested with synthetic events. */
static void process(Dev *d, const struct input_event *e, PG_InjectFn inject)
{
    if (e->type == EV_ABS) {
        if (d->mt) {
            if (e->code == ABS_MT_SLOT) { d->cur = e->value >= 0 && e->value < MAX_SLOT ? e->value : -1; return; }
            if (d->cur < 0) return;
            Slot *s = &d->s[d->cur];
            if (e->code == ABS_MT_TRACKING_ID) {
                if (e->value < 0) s->active = false;
                else { s->active = true; s->have_x = s->have_y = false; }
            } else if (e->code == ABS_MT_POSITION_X) { s->x = norm(e->value, d->min_x, d->max_x); s->have_x = true; s->moved = true; }
            else if (e->code == ABS_MT_POSITION_Y)   { s->y = norm(e->value, d->min_y, d->max_y); s->have_y = true; s->moved = true; }
        } else {
            Slot *s = &d->s[0];
            if (e->code == ABS_X) { s->x = norm(e->value, d->min_x, d->max_x); s->have_x = true; s->moved = true; }
            else if (e->code == ABS_Y) { s->y = norm(e->value, d->min_y, d->max_y); s->have_y = true; s->moved = true; }
        }
    } else if (e->type == EV_KEY && e->code == BTN_TOUCH && !d->mt) {
        d->s[0].active = e->value != 0;             /* position events may come before or after BTN_TOUCH in a report */
    } else if (e->type == EV_SYN && e->code == SYN_REPORT) {
        int64_t base = ((int64_t)d->index + 1) << 20;
        for (int i = 0; i < (d->mt ? MAX_SLOT : 1); i++) {
            Slot *s = &d->s[i];
            if (s->active && !s->reported && s->have_x && s->have_y) { inject(PG_INJECT_DOWN, base + i, s->x, s->y); s->reported = true; s->moved = false; }
            else if (s->active && s->reported && s->moved) { inject(PG_INJECT_MOVE, base + i, s->x, s->y); s->moved = false; }
            else if (!s->active && s->reported) { inject(PG_INJECT_UP, base + i, s->x, s->y); s->reported = false; s->have_x = s->have_y = false; }
        }
    }
}

static void release_all(Dev *d, PG_InjectFn inject)
{
    int64_t base = ((int64_t)d->index + 1) << 20;
    for (int i = 0; i < MAX_SLOT; i++)
        if (d->s[i].reported && inject) inject(PG_INJECT_UP, base + i, d->s[i].x, d->s[i].y);
    memset(d->s, 0, sizeof d->s);
}

static bool bit(const unsigned long *a, int b) { return (a[b / (int)(8 * sizeof(long))] >> (b % (int)(8 * sizeof(long)))) & 1; }

/* Does this event device look like a touch screen? Fills d on success. */
static bool probe(int fd, Dev *d, char *why, size_t whyn)
{
    unsigned long ev[(EV_MAX / (8 * sizeof(long))) + 1] = { 0 }, abs_[(ABS_MAX / (8 * sizeof(long))) + 1] = { 0 };
    unsigned long key[(KEY_MAX / (8 * sizeof(long))) + 1] = { 0 }, prop[(INPUT_PROP_MAX / (8 * sizeof(long))) + 1] = { 0 };
    if (ioctl(fd, EVIOCGBIT(0, sizeof ev), ev) < 0 || !bit(ev, EV_ABS)) { snprintf(why, whyn, "no absolute axes"); return false; }
    ioctl(fd, EVIOCGBIT(EV_ABS, sizeof abs_), abs_);
    ioctl(fd, EVIOCGBIT(EV_KEY, sizeof key), key);
    ioctl(fd, EVIOCGPROP(sizeof prop), prop);
    bool direct = bit(prop, INPUT_PROP_DIRECT), pointer = bit(prop, INPUT_PROP_POINTER);
    struct input_absinfo ax, ay;
    if (bit(abs_, ABS_MT_POSITION_X) && bit(abs_, ABS_MT_POSITION_Y)) {
        d->mt = true;
        if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &ax) < 0 || ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &ay) < 0) return false;
    } else if (bit(abs_, ABS_X) && bit(abs_, ABS_Y) && bit(key, BTN_TOUCH) && !bit(key, BTN_TOOL_PEN)) {
        d->mt = false;
        if (ioctl(fd, EVIOCGABS(ABS_X), &ax) < 0 || ioctl(fd, EVIOCGABS(ABS_Y), &ay) < 0) return false;
    } else { snprintf(why, whyn, "not a touch digitiser"); return false; }
    if (pointer && !direct) { snprintf(why, whyn, "touchpad, not a touch screen"); return false; }
    d->min_x = ax.minimum; d->max_x = ax.maximum; d->min_y = ay.minimum; d->max_y = ay.maximum;
    return true;
}

static void scan(bool verbose)
{
    for (int n = 0; n < 32 && ndev < MAX_DEV; n++) {
        char path[48]; snprintf(path, sizeof path, "/dev/input/event%d", n);
        bool have = false;
        for (int i = 0; i < ndev; i++) if (devs[i].index == n) have = true;
        if (have) continue;
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        Dev d; memset(&d, 0, sizeof d);
        char name[64] = "?", why[48] = "";
        ioctl(fd, EVIOCGNAME(sizeof name), name);
        d.fd = fd; d.index = n; snprintf(d.name, sizeof d.name, "%s", name);
        if (probe(fd, &d, why, sizeof why)) {
            devs[ndev++] = d;
            pg_log("evdev fallback: reading touch device %s '%s' (%s, x %d..%d y %d..%d)", path, name, d.mt ? "multitouch" : "single touch", d.min_x, d.max_x, d.min_y, d.max_y);
        } else { if (verbose) pg_log("evdev: skipping %s '%s' (%s)", path, name, why); close(fd); }
    }
}

void pg_evdev_start(void) { ndev = 0; last_scan_ms = SDL_GetTicks(); scan(true); }

void pg_evdev_poll(PG_InjectFn inject)
{
    uint32_t now = SDL_GetTicks();
    if (now - last_scan_ms > 3000) { last_scan_ms = now; scan(false); }       /* frame plugged in after start */
    for (int i = 0; i < ndev; i++) {
        struct input_event ev[32];
        for (;;) {
            ssize_t n = read(devs[i].fd, ev, sizeof ev);
            if (n < 0) {
                if (errno == EAGAIN || errno == EINTR) break;
                pg_log("evdev: lost %s", devs[i].name);                           /* unplugged */
                release_all(&devs[i], inject); close(devs[i].fd); devs[i] = devs[--ndev]; i--; break;
            }
            if (n == 0) break;
            for (size_t k = 0; k < (size_t)n / sizeof ev[0]; k++) process(&devs[i], &ev[k], inject);
        }
    }
}

void pg_evdev_stop(PG_InjectFn inject)
{
    for (int i = 0; i < ndev; i++) { release_all(&devs[i], inject); close(devs[i].fd); }
    ndev = 0;
}

int pg_evdev_count(void) { return ndev; }
