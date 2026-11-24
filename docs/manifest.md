# Game manifest: `games/<id>/game.ini`

The manifest is a **contract**: `tools/pgcat` validates it, the `pg` subscriber and any launcher read it.
Change the format only together with this document and `tools/pgcat.c`.

Format: flat `key = value` lines, one per line. `#` starts a comment line. UTF-8, no control characters,
max 255 bytes per value. No sections, no quoting. Unknown keys are rejected unless they start with `x-`
(your own metadata).

| key | required | rule |
|---|---|---|
| `id` | yes | `[a-z][a-z0-9-]{1,31}`, **equal to the directory name**. Reserved: `installed saves src build subscriptions channels games tools sdk docs` |
| `name` | yes | display name, <= 40 chars |
| `version` | yes | `MAJOR.MINOR.PATCH`, numbers only |
| `author` | yes | <= 80 chars |
| `license` | yes | SPDX id from: `MIT BSD-2-Clause BSD-3-Clause ISC Zlib Apache-2.0 CC0-1.0 Unlicense GPL-2.0-or-later GPL-3.0-or-later` |
| `description` | yes | one sentence a parent understands, <= 140 chars |
| `sdk_api` | yes | integer: the SDK API version the game was written against (currently `1`; `sdk/pg_version.h`) |
| `players` | yes | `1`..`4` or a range such as `1-4` (children touching at once) |
| `tags` | no | comma separated, lowercase letters/digits/dashes, at most 6 |
| `tested` | no | `none` (default), `desktop` (headless/desktop only), `pi` (maintainer tested on the real Pi + touch frame). **Only the maintainer sets `pi`.** |

Files that must exist next to it:

- `src/` with at least one `.c` file (the shared build compiles `src/*.c` and `src/*/*.c`).
- `icon.bmp`: a **256x256** uncompressed BMP, 24 or 32 bits per pixel, generated with `make icon GAME=<id>`
  (the centre 540x540 of a headless frame). Launchers show it; `SDL_LoadBMP` reads it without SDL_image.

Channel membership is **not** in the manifest (a contributor must not be able to promote their own game).
It lives in `channels/<name>.txt`, one id per line, edited only by the maintainer; a game is listed in at most
one channel file, and games listed nowhere are in the implicit `incubating` channel.

Example:

```ini
id = ball-pit
name = Ball Pit
version = 1.0.0
author = Pi Games contributors
license = MIT
description = Scoop, fling and pour bouncy balls in a giant toy-box ball pit.
sdk_api = 1
players = 1-4
tags = physics, balls
tested = desktop
```

Check it with `make validate` (or `build/tools/pgcat --root . validate`).
