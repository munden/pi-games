# AGENTS.md - rules for every agent (and human) working in this repo

Read this whole file before you change anything. The rules are binding. If a task seems to need
breaking one, stop and tell the maintainer (the repo owner) instead of bending it. Anything not
forbidden here is allowed, but "it compiles" is never the bar: **a 4-year-old has to love it and
it has to look great.**

## 1. What this repo is

A collection of small games and toys for one specific device: a **Raspberry Pi Zero 2 W** driving a
**27" infrared multi-touch frame (10 contacts, USB) standing on an easel**, HDMI video and HDMI audio.
The player is a **4-year-old**. Physics is fun, so most toys are physics toys.

- Every game is **self-contained in `games/<id>/`** and built by one shared build.
- A separate launcher/subscriber program picks games by **id** or by **channel** (`tools/pg`,
  contract in `docs/subscribing.md`). Other people submit games by pull request.
- The **maintainer decides where a merged game lives**: `channels/*.txt` lists game ids per channel
  (`stable`, `community`, ...). A game listed nowhere is in the implicit `incubating` channel.
  Contributors and agents never edit `channels/`.

## 2. The device (design for exactly this)

| Thing | Fact |
|---|---|
| Computer | Pi Zero 2 W: 4x Cortex-A53 1 GHz, 512 MB RAM shared with the GPU, VideoCore IV (GLES 2.0 via KMS/V3D), SD card, 2.4 GHz Wi-Fi only |
| OS | Raspberry Pi OS **Lite** (Debian 12 "Bookworm" or newer), no desktop, no X11, no Wayland. 64-bit preferred; 32-bit must also build. Managed over SSH |
| Language | **Plain C (gnu11)** compiled with gcc. No C++, no scripting languages in games or the SDK. Tooling in `tools/` is C or POSIX `sh` |
| Libraries | **SDL2 only** (API floor 2.26.0 = Bookworm) + libc + libm. No SDL3, SDL_image, SDL_ttf, SDL_mixer, SDL_net, desktop OpenGL, raw GLES/EGL, Vulkan, X11, GTK/Qt, curl, pthreads |
| Video | SDL **KMSDRM** straight to the HDMI display, 1920x1080 typical. The SDK picks the driver and renderer (`opengles2`); games never do |
| Canvas | Your game draws into a fixed **960x540** canvas (`PG_W` x `PG_H`). The SDK scales it (2x on 1080p, bilinear). **1 canvas px is about 0.62 mm, ~16 px per cm** on the 27" panel |
| Input | **Touch only**: up to 10 simultaneous contacts, no hover, no pressure, no contact size. Palms and forearms also register. Keyboard exists only for developers (Esc quits) |
| Audio | HDMI through ALSA, 48 kHz stereo. The SDK owns the device; all sound is synthesised |
| Run mode | One game at a time owns the display (DRM master). Started from SSH or a launcher. No stdin, no terminal interaction, no prompts |
| Display | Upright, landscape, **gravity is "down" on screen** |

If you cannot measure something on the real Pi (you almost never can), design with margin and say so (section 9).

## 3. The player: a 4-year-old (hard requirements)

1. **Touch only.** Everything works with fingers, fists and palms. Never require keyboard, mouse or gamepad.
2. **No reading.** No menus, instructions, scores, timers or labels needed to play. Pictures and motion only.
   Letters/numbers may appear as toy content (alphabet blocks), never as instructions.
3. **Nothing can go wrong.** No game over, lives, "wrong", timers, countdowns or failure sounds. Every touch is fine.
4. **Instant, rewarding response.** Every touch gets a visible and audible reaction on the very next frame.
5. **10 fingers and palms are normal.** Handle all 16 tracked contacts, drag, tap, hold, two hands at once, a hand
   slapped flat, a forearm. Nothing may crash, stall, or become irreversible. Never need a precise position.
6. **Big targets.** Any button-like control >= 96 px (6 cm); anything meant to be grabbed >= 64 px; add >= 12 px
   of hit-test padding. Keep controls to a few big pictorial buttons. Nothing important within 96 px of the two
   top corners: they belong to the parent gate.
7. **Alive when idle.** Something gentle is always moving, and within 3 s an inviting cue shows where to touch
   (a bobbing toy, a pointing hand sprite). A child who walks up to the screen must know what to do without help.
8. **Only taps and drags.** No long-press, double-tap, pinch, rotate, edge swipes, multi-finger gestures.
9. **Gentle sound.** Use `pg_sfx_*`, `pg_tone`, `pg_note_hz` (pentatonic: any mix sounds nice). Nothing sudden or loud,
   no sound over 1.5 s, no harsh/high shrieks, no unstoppable music, no speech. `vol` <= 0.8. Never touch the master volume.
   Rate-limit collision sounds (the presets already do). Audio may be missing: the game must work in silence.
10. **No flashing.** No full-screen flashes, nothing large flickering faster than 3 Hz, no strobing sparkles.
11. **Friendly content.** Toys, animals, food, shapes, weather, music. Nothing scary, violent, weapon-like or realistic.
12. **Private.** No network, no accounts, no tracking, no ads, no links, no files written about the child.
13. **Leaving is the parent's job.** The SDK implements the exit (hold both top corners for 2 s) and Esc for developers.
    Never build your own exit, never block it, never use the top-corner areas.
14. **Calm pacing.** No sudden camera moves, nothing that chases or startles the child, no fast spinning screen.

## 4. The look: "PlayStation 1 or better"

Every game must look like a PS1-era 3D game (think Spyro, Crash, Klonoa, Rayman 3D) **or better**. Flat coloured
discs and rectangles drawn with 2D primitives are not acceptable as the main content.

Required:
- **A real 3D scene** drawn with `sdk/pg3d.h`: perspective camera, textured Gouraud-lit low-poly meshes, a sky or room
  backdrop, a floor and/or back wall, blob shadows. The playfield may be 2.5D (physics in a plane at z=0) but it must
  read as 3D: visible depth, objects with thickness, a gently swaying camera or parallax.
- **Painted textures on every surface** (`pg_img_*` then `pg_img_ps1()` for the 15-bit dither, 16..128 px, nearest
  filter), not flat vertex colours. Bevelled / rounded forms (`pg_mesh_bevel_box`, spheres of at least 12x8, tori).
- **Consistent lighting** (`pg3d_light`), highlights on glossy toys (`pg3d_shine`), shadows under things.
- **Colour**: saturated toy palette with value contrast, sky gradients, never a plain black background, no black outlines.
- **Juice**: sparkles / confetti / stars, squash-and-stretch via the model matrix, everything animates a little.
- Chunky texels are the style; they must look intentional and clean. Painter's-sort artefacts must not be visible:
  keep meshes convex-ish, put floors/walls/decals in `PG3D_BG`, do not interpenetrate large meshes.
- "Better than PS1" is welcome: smoother shading, more polygons (within budget), additive glows, soft shadows.

**How to verify:** `make shot GAME=<id>`, then **open `build/shots/<id>.png` and look at it** (and an interaction
frame via a `test.touch` script). If it could not pass as a screenshot of a PS1-era game, it is not done.
The reference for the bar is `games/_template` (`make shot GAME=_template`).

## 5. Budgets (Pi Zero 2 W, 60 fps vsync)

| Resource | Limit |
|---|---|
| Triangles per frame | <= 6,000 typical, **<= 10,000 peak** (`tris_max` in the smoke output) |
| Your CPU per tick on a dev machine | `update + cpu3d` <= 1.5 ms combined (the Pi is ~8x slower; ~12 ms = the whole frame). Printed by `make smoke` as `update=` and `cpu3d=` |
| Physics | <= 250 dynamic bodies, shapes >= 10 px (smaller fast ones tunnel) |
| Textures | each <= 128x128, atlas is 1024x1024 total; aim for <= 24 textures; all painted at init |
| Memory | < 100 MB resident (typically < 30). No malloc/free per frame; allocate in `init`, free in `shutdown` |
| Draw calls | go through `pg3d_*` (a handful of batches). No per-pixel CPU drawing every frame beyond a 320x180 buffer |
| Files | no assets at all: everything procedural. A game dir holds `game.ini`, `icon.bmp`, `src/*.c|h`, optional `test.touch`, `README.md` |
| Disk writes | none |
| Threads | none (the SDK owns the audio thread) |
| CPU use | never busy-wait; the SDK's loop sleeps/vsyncs |

Degrade gracefully: `pg_frame_ms()` is the smoothed CPU time of the last frames; if it climbs above ~12 on the Pi,
stop spawning new bodies / fade old ones out (children will pile things up forever).

## 6. Repository layout and isolation

```
AGENTS.md CLAUDE.md README.md CONTRIBUTING.md Makefile
sdk/        platform: pg.h (runtime, touch, audio, 2D), pg3d.h (PS1 3D), pg_phys.h (2D physics), pg_img.h (textures)
games/<id>/ game.ini  icon.bmp  src/*.c  [test.touch] [icon.touch] [README.md]
games/_template/   the starter game (copy with: make new GAME=my-game)
channels/   <name>.txt: one game id per line. MAINTAINER ONLY
tools/      pgcat.c (manifest tool), pg (subscriber CLI), lint.sh, smoke.sh, selftest.sh, smoke.touch
tests/      unit tests (physics)
docs/       manifest.md subscribing.md testing.md pi-setup.md maintainers.md
```

- **A game change touches only `games/<id>/`.** Never edit another game. A pull request changes one game.
- `sdk/`, `tools/`, `tests/`, `docs/`, `channels/`, `.github/`, `Makefile`, `AGENTS.md`, `CLAUDE.md` are maintainer-owned.
  If you need something from the SDK, build it inside your game if it is small, otherwise write the request in your
  final message. Do not modify the SDK from a game task.
- No per-game Makefile. Sources in `games/<id>/src/` (`*.c`, one subdirectory level) are built with the shared flags
  (`-std=gnu11 -O2 -Wall -Wextra`, `-Werror` under `make check`).
- Third-party code is not allowed (no vendored libraries). Everything is yours or SDK.
- Ids: `[a-z][a-z0-9-]{1,31}`, equal to the directory name; reserved: `installed saves src build subscriptions channels games tools sdk docs`.

## 7. Writing a game

Start from the template: `make new GAME=my-game`. The contract (read the headers, they are the API reference):

```c
#include "pg3d.h"                       /* includes pg.h; add pg_phys.h if you use physics */
static const PG_Game game = { "my-game", "My Game", init, update, render, shutdown };
int main(int argc, char **argv) { return pg_run(&game, argc, argv); }
```
- `init(r)`: paint textures (`pg_img_*` -> `pg3d_texture`), build meshes, create worlds. Return 0. Seed nothing: use `pg_rand*`.
- `update()`: called at a fixed 60 Hz. Read `pg_touches()`; touches arrive as DOWN (one tick), HELD, UP (one tick).
  Map a touch to the z=0 plane with `pg3d_plane_z(t->x, t->y, 0, &p)`; physics uses +y up, same as pg3d.
- `render(r)`: `pg3d_begin(&cam)`, submit meshes/sprites/shadows, `pg3d_end(r)`; overlays last (`pg_disc`, `pg_text`...).
  If you change the render target, restore `pg_canvas()`.
- **Never** call `SDL_Init/SDL_Quit/SDL_CreateWindow/SDL_CreateRenderer/SDL_RenderPresent/exit/abort`, set
  `SDL_VIDEODRIVER`, `system`, `popen`, `fork`, sockets, `dlopen`. `make lint` fails the build if you do.
  Unrecoverable init problems: `pg_fatal(...)`. Logging: `pg_log` (never per frame).
- C quality: `-Wall -Wextra` clean, no undefined behaviour, check allocations, fixed-width ints for data, no VLAs on big
  sizes, no global state outside `static` file scope, free what you allocate. `make check SAN=1` must pass under ASan/UBSan.
- Physics: `pg_phys.h`. Gravity ~ (0,-900) px/s^2. Fingers that shove things are kinematic circles (density 0, set vx/vy
  each tick, speed-capped). Hold-and-drag a block with `pg_body_grab_step`. Use the contact callback for sounds.
- 2D shapes with `pg_*` helpers are for overlays/effects, not the main look.

## 8. Manifest (`games/<id>/game.ini`)

```
id = ball-pit                      # = directory name
name = Ball Pit                    # <= 40 chars
version = 1.0.0                    # MAJOR.MINOR.PATCH
author = Your Name                 # <= 80 chars
license = MIT                      # MIT BSD-2-Clause BSD-3-Clause ISC Zlib Apache-2.0 CC0-1.0 Unlicense GPL-2.0-or-later GPL-3.0-or-later
description = One sentence a parent understands.   # <= 140 chars
sdk_api = 1                        # SDK version you wrote against
players = 1-4                      # 1..4 or a range
tags = physics, balls              # optional, <= 6, lowercase
tested = desktop                   # none | desktop | pi   (agents never write "pi")
```
Flat `key = value`, `#` comment lines, UTF-8, unknown keys rejected unless they start `x-`. Full rules: `docs/manifest.md`.
`icon.bmp` is a 256x256 24/32-bit BMP, produced by `make icon GAME=<id>` from a headless run (`icon.touch` optional
to pose the scene). `make validate` checks all of this.

## 9. Build, verify, and what "done" means

```sh
sudo apt install build-essential pkg-config libsdl2-dev     # dev machine or the Pi
make                          # SDK + tools + all games   -> build/
make GAME=ball-pit            # one game
make run GAME=ball-pit        # run it (desktop window; mouse acts as one finger. On the Pi: KMS/DRM)
make shot GAME=ball-pit       # headless screenshot -> build/shots/ball-pit.png   <- LOOK AT IT
make icon GAME=ball-pit       # games/ball-pit/icon.bmp
make check GAME=ball-pit      # lint + manifest + -Werror build + headless smoke + unit tests
make check SAN=1              # same under AddressSanitizer + UBSan (do this before you finish)
```
Headless testing is the only testing agents can do, so use it properly (`docs/testing.md`):
`--headless --frames N --touch-script F --screenshot P.png --seed N`. A touch script injects fingers by canvas
pixel (`tap T SLOT X Y`, `hold`, `drag T DUR SLOT X0 Y0 X1 Y1`, `wave`, `down/move/up`, `fuzz T DUR N`).
Put an interesting one in `games/<id>/test.touch` (it replaces `tools/smoke.touch` for your game) so the smoke test and
screenshot exercise your real interactions, including 10 fingers at once (`fuzz`).

**A game is done when all of these hold:**
1. `make check GAME=<id>` and `make check GAME=<id> SAN=1` pass.
2. You looked at screenshots of the idle state and of an interaction (several fingers) and they meet section 4.
3. Section 3 holds, point by point. Check each one honestly against your code.
4. `tris_max`, `update`, `cpu3d` in the smoke output are inside section 5.
5. `game.ini` and `icon.bmp` are valid; `tested = desktop`.
6. Your final message lists: what the game does, what a child can do, anything you could not verify, and SDK gaps.

**Be honest about hardware.** You cannot test on the Pi or the touch frame. Do not claim you did, never write
`tested = pi`, and say plainly that frame time, touch feel and audio levels are unverified on the device.

## 10. Submitting and merging

- Work on a branch `game/<id>`; open one pull request that touches only `games/<id>/`. CI runs `make check`,
  `tools/lint.sh`, and a scope check (one game per PR, nothing outside it).
- The maintainer reviews the **code** (it will run on a child's device), the screenshots, then decides the channel
  by editing `channels/*.txt` (a game is listed in at most one). New games start in `incubating` (no channel file).
- Agents do not merge, do not push to the default branch, do not edit `channels/`, and do not push or open PRs
  unless the maintainer asked.
- Other contributors follow `CONTRIBUTING.md`, which is this file's rules in short form.

## 11. Trust and safety

Games are native code that runs as the user on a device a child plays on. Hence: no processes, network, dynamic
loading, filesystem access (SDK files aside), no inline assembly, no `-march=native`, no obfuscation. Reviewers
read every line. A subscriber only builds games from its own clone of this repo and should subscribe to
`@stable` unless it trusts the maintainer's review of `community`/`incubating`.

## 12. Quick "don't" list

No 2D-flat look. No text required. No game over. No timers. No exits of your own. No SDL init/window/present.
No edits outside `games/<id>/`. No assets/binaries. No C++/Python. No network. No loud/harsh/long sounds. No flashing.
No tiny targets. No per-frame allocation. No claims of Pi testing. No channel edits.
