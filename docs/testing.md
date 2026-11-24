# Testing games without the hardware

Nobody working on this repo from a laptop or a cloud session can touch the real frame, so the SDK makes the
game testable headless and **deterministic**. Use it properly: it is also how you prove a game works.

## Command line (every game)

| flag | meaning |
|---|---|
| `--headless` | dummy audio/video, software renderer on a plain surface, exactly one tick per frame, no sleeping, RNG seeded to 1 |
| `--frames N` | stop after N update ticks (60 per second) |
| `--touch-script FILE` | inject fingers from a script (below) |
| `--screenshot P.png` | write the last frame (960x540, `.png` or `.bmp`) |
| `--icon P.bmp` | write the 256x256 launcher icon (centre of the last frame) |
| `--seed N` | seed `pg_rand` |
| `--windowed` / `--fullscreen` | desktop development; the mouse acts as one finger when there is no touch screen |
| `--fps` | log fps and CPU ms per frame every 2 s (use this on the Pi) |
| `--calibrate` | touch calibration for the grown-up; writes `~/.config/pi-games/touch.cal` (or `$PG_TOUCH_CAL`) |
| `--info` | print SDL version, video driver, displays, audio devices and **touch devices** (SDL's and the native fallback's) |

At the end of a `--frames` run the SDK prints
`[pg] done: ticks= avg_work= max_work= update= cpu3d= tris_avg= tris_max=`.
`update` is your `update()` ms per tick, `cpu3d` the CPU time spent in pg3d (transform, light, sort), `tris_*`
triangles per frame. `avg_work` in headless mode includes software rasterising and is **not** a Pi number; the Pi
runs `update + cpu3d` roughly 8x slower than a modern dev machine (an estimate, measure with `--fps` on the device).

## Touch scripts

One command per line, `#` comments. `T` is a tick (60/s), `SLOT` 0..15 names a virtual finger, `X,Y` are canvas
pixels (0..959, 0..539, y down).

```
tap   T SLOT X Y                          touch down at T, up at T+4
hold  T DUR SLOT X Y                      down at T, up at T+DUR
drag  T DUR SLOT X0 Y0 X1 Y1              straight drag over DUR ticks
wave  T DUR SLOT CX CY R TURNS            circle of radius R, TURNS revolutions
down  T SLOT X Y  /  move T SLOT X Y  /  up T SLOT X Y      raw events
fuzz  T DUR N                             N fingers making random drags for DUR ticks (10 = the whole frame)
```
Put your scenario in `games/<id>/test.touch`; it replaces `tools/smoke.touch` for your game in `make smoke`,
`make shot`, `make check`. End it with a `fuzz 10`-style stress. Optional `games/<id>/icon.touch` poses the
icon. Useful loop for screenshots at several moments:

```sh
for n in 60 300 900 1800; do
  build/games/ball-pit/ball-pit --headless --frames $n --touch-script games/ball-pit/test.touch --screenshot /tmp/s$n.png
done
```

## Gates

- `make check [GAME=id]` = lint + manifest + `-Werror` build + unit tests + tooling self-test + headless smoke.
  The smoke fails on a non-zero exit, a `FATAL`, an exceeded triangle budget or a sanitizer report.
- `make check SAN=1` does the same with AddressSanitizer + UBSan (own build dir `build/check-san`).
- `tests/phys_test.c` covers the physics engine (bounce, 8-box tower, pyramid, pile, ramp, kinematic push, grab, events).
- `tests/evdev_test.c` covers the native touch fallback with synthetic kernel multitouch events.
- `tools/selftest.sh` covers manifests, channels, the PR scope rule and the `pg` subscribe/sync/run/prune flow.

## What headless cannot tell you (check on the device)

Touch feel and latency, ghost touches from the IR frame, real frame time on the Pi, audio loudness through the
TV/monitor, heat, KMS mode selection. `docs/pi-setup.md` and `docs/maintainers.md` list the checks.
