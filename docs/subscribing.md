# Subscribing to games (the contract for launchers and other programs)

A *subscriber* is any program that wants some of the games in this repository: the bundled `tools/pg`
CLI, a touch launcher that shows big icons, a provisioning script. This page is what they can rely on.

## 1. Where games come from

A clone of this repository. Subscribers build from **their own clone** (`git pull --ff-only` to update);
nothing is downloaded or executed from anywhere else. Games are native programs that run as the user, so only
subscribe to what you trust: `@stable` is what the maintainer has reviewed *and* tested on the device;
`@community` is reviewed; `@incubating` is merged but unreviewed for the device. See `docs/maintainers.md`.

## 2. Selecting games

A **token** is a game id (`ball-pit`) or a channel (`@stable`). Channels come from `channels/*.txt`
(maintainer-edited) plus the implicit `@incubating` = every game listed nowhere. A game is in exactly one channel.

```sh
tools/pg list                      # what exists, with channel and your subscription
tools/pg subscribe @stable ball-pit
tools/pg sync                      # git pull, build what you subscribed to, install, prune the rest
tools/pg run ball-pit              # run one (exec, cwd = its install dir)
tools/pg unsubscribe ball-pit
```

State lives in `$PG_HOME` (default `${XDG_DATA_HOME:-~/.local/share}/pi-games`). Other programs may read or
write these files directly:

```
$PG_HOME/subscriptions          one token per line (# comments allowed)
$PG_HOME/installed/index.tsv    id <TAB> version <TAB> name <TAB> players <TAB> install dir   (rewritten atomically by every sync)
$PG_HOME/installed/<id>/
    <id>                        the executable. Run it with cwd = this directory
    game.ini                    manifest copy (docs/manifest.md)
    icon.bmp                    256x256 icon for menus (SDL_LoadBMP)
    README.md                   optional, what to check on the device
    .pg-installed               key=value: version, commit, built (UTC), sdk_api
```

`pg sync` installs each game atomically (the previous copy stays until the new one is complete), skips games
whose manifest is invalid, keeps an old install if a build fails, and removes installs that are no longer
subscribed (`--no-prune` to keep them, `--no-pull` to skip git).

## 3. Catalog (machine readable)

`tools/pg catalog` (= `build/tools/pgcat --root . catalog`) prints JSON for every valid game:

```json
{ "schema": 1, "sdk_api": 1, "channels": ["community", "stable", "incubating"],
  "games": [ { "id": "ball-pit", "name": "Ball Pit", "version": "1.0.0", "author": "...", "license": "MIT",
               "description": "...", "players": "1-4", "sdk_api": 1, "tested": "desktop",
               "tags": ["physics"], "channel": "incubating", "path": "games/ball-pit",
               "icon": "games/ball-pit/icon.bmp" } ] }
```
`schema` changes only when fields are removed or change meaning; new fields may appear at any time, so ignore
unknown ones. `tools/pg list` and `pgcat list` give the same data as tab-separated text.

## 4. Running a game from a launcher

- Start the executable directly (`fork`/`exec`), cwd = its install dir. No arguments needed.
- Do **not** set `SDL_VIDEODRIVER`; the game picks KMS/DRM itself when there is no desktop. Only one program may own
  the display at a time: the launcher must release it (exit or close its SDL video) before starting a game and may
  take it back when the game exits.
- Exit codes: `0` = the parent left via the exit gesture (hold both top corners for 2 s) or Esc; `1`/`2` = the game
  could not start (video/init failure, message on stderr prefixed `[pg]`); anything else = crash. Re-show the menu in
  every case. Stderr is for logs; there is no stdin.
- Every game accepts `--info`, `--fps`, `--windowed`; `--calibrate` runs the touch calibration for the grown-up.
  Do not pass `--headless` outside testing.

## 5. Compatibility

`sdk_api` in the manifest is the SDK version the game was built against. The repository builds every game from
source against the SDK in the same checkout, so a subscriber never mixes versions; `sdk_api` matters to people who
copy games between checkouts. A change that could break existing games bumps `PG_API_VERSION` and is announced in
the pull request that makes it.
