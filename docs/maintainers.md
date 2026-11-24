# Maintainer guide

You decide what enters the repository, which channel it lives in, and when something is safe for the child.

## Reviewing a submitted game

1. `git fetch` the branch; the CI scope check already guarantees it touches only `games/<id>/`.
2. **Read every line of `src/`.** It runs as the user on a device a child uses. Look for anything outside the rules
   (processes, files, network, loops that can stall, unbounded allocation, per-frame `malloc`). `make lint` catches the obvious.
3. `make check GAME=<id>` and `make check GAME=<id> SAN=1`; read the `[pg] done:` line against the budgets in AGENTS.md section 5.
4. `make shot GAME=<id>` and look: **PS1 or better** (AGENTS.md section 4), nothing flat or empty, idle state inviting.
5. Walk AGENTS.md section 3 (the 4-year-old) point by point against the code.
6. Merge into `incubating` (do nothing else: a game listed in no channel file is incubating).

## Choosing where it goes

`channels/<name>.txt`, one id per line, a game in at most one file:

| channel | meaning | requirement |
|---|---|---|
| `incubating` (implicit) | merged, CI green | code read once |
| `community` | reviewed | steps 1-5 above + you ran it on a desktop |
| `stable` | safe to hand to the child | tested on the real Pi + touch frame (below); you set `tested = pi` |

Promote or demote by moving the id between files in a commit. Retire a game by removing the directory (and its channel line).
Subscribers follow with `tools/pg sync`. Add a new channel by adding `channels/<name>.txt` (lowercase, `incubating` is reserved).

## Device test before `stable`

On the Pi with the touch frame (see `docs/pi-setup.md`): `pg run <id> --fps`.
- Frame time: `work` under ~12 ms with 10 fingers busy and the scene piled up; no stutter after 10 minutes.
- Touch: ten fingers, palms, a fist, a forearm: nothing breaks, nothing freezes, no accidental exit.
- Parent gate: both top corners for 2 s exits; one corner never does.
- Sound: pleasant at the TV's normal volume; nothing sudden; silent if the HDMI audio is unplugged.
- Looks: no visible sorting artefacts, text-free, nothing within 96 px of the top corners is a control.
- 30 minute soak with periodic random play: no growth in memory (`ps -o rss`), temperature below throttling.
Then set `tested = pi` in `game.ini` and add the id to `channels/stable.txt`.

## Repository settings (GitHub)

Recommended (not configurable from inside the repo): protect the default branch, require the `ci` workflow and a
review from the code owner, disallow force pushes. `.github/CODEOWNERS` makes you the owner of everything.
The `scope` job rejects PRs that touch more than one game or anything outside it, except from the repository owner.

## Changing the platform

`sdk/`, `tools/`, `Makefile`, docs and `AGENTS.md` are yours. A change that can break existing games bumps
`PG_API_VERSION` in `sdk/pg_version.h` (and the validator's accepted range) and gets its own PR.
