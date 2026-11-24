# pi-games

Games and toys for a **Raspberry Pi Zero 2 W** driving a **27" infrared multi-touch frame on an easel**
(10 touch points over USB, HDMI video and sound), for a **4-year-old**. Physics is fun, and everything should look
like a **PlayStation 1 game or better**.

Plain C, SDL2 on KMS/DRM, Raspberry Pi OS Lite, no desktop. Each game lives in its own directory and a launcher
program can subscribe to the ones it wants; other people can submit games, and the maintainer decides which
channel each one is merged into.

## The games

| id | what a child does |
|---|---|
| `ball-pit` | scoop, fling and pour bouncy balls in a giant toy-box ball pit |
| `block-tower` | grab chunky toy blocks, build towers, knock them down |
| `sand-pour` | pour rainbow sand through pegs and funnels, open the trapdoor to drain it |
| `draw-roll` | draw ramps with ten fingers and watch balls roll down them |
| `balloon-party` | pop balloons that play notes, or bat them around |
| `jelly-blobs` | poke, stretch and fling squishy jelly creatures with faces |

(`games/_template` is the starter and the visual reference.) Every game starts in the implicit `incubating`
channel; promote it by listing its id in `channels/community.txt` or `channels/stable.txt`.

## Try it on a dev machine

```sh
sudo apt install build-essential pkg-config libsdl2-dev
make                          # SDK, tools, all games -> build/
make run GAME=ball-pit        # a window; the mouse acts as one finger
make shot GAME=ball-pit       # headless screenshot -> build/shots/ball-pit.png
make check                    # everything CI runs
```

## Subscribing (the launcher side)

```sh
tools/pg list
tools/pg subscribe @stable ball-pit
tools/pg sync                 # pull, build, install under ~/.local/share/pi-games/installed/
tools/pg run ball-pit
```
The contract for launchers (install layout, catalog JSON, exit codes) is `docs/subscribing.md`.

## Layout

| path | what |
|---|---|
| `AGENTS.md` | **the rules every agent and contributor follows** (`CLAUDE.md` points to it) |
| `sdk/` | the platform: runtime + touch + audio (`pg.h`), PS1-style 3D (`pg3d.h`), 2D physics (`pg_phys.h`), texture painter (`pg_img.h`) |
| `games/<id>/` | one self-contained game: `game.ini`, `icon.bmp`, `src/`, `test.touch` |
| `channels/` | maintainer-owned lists of game ids per channel |
| `tools/` | `pg` (subscriber), `pgcat` (manifests/catalog), lint, smoke tests, self-test |
| `docs/` | manifest, subscribing, testing, Pi setup, maintainer guide |
| `tests/` | physics unit tests |

## Status

Everything is verified headless (software renderer, scripted touches, sanitizers). **Nothing has been run on the
real Pi or the touch frame yet**: frame time, touch feel, audio level and the setup steps in `docs/pi-setup.md` are
unconfirmed. See `docs/maintainers.md` for the device test that gates the `stable` channel.

Licensed under the MIT License (`LICENSE`). Contributing: `CONTRIBUTING.md`.
