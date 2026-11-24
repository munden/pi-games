# Contributing a game

Thanks! The rules are in [AGENTS.md](AGENTS.md); this is the short version.

**The device:** Raspberry Pi Zero 2 W, SDL2 on KMS/DRM, a 27" 10-point **touch** frame on an easel, HDMI sound.
**The player:** a 4-year-old. **The look:** PlayStation 1 or better.

1. Fork, then `make new GAME=my-game` (id: lowercase letters, digits, dashes).
2. Write it in plain C with the SDK in `sdk/` (read `sdk/pg.h`, `pg3d.h`, `pg_phys.h`; `games/_template` is the model).
   Touch only. No reading needed. Nothing can go wrong. Gentle sound. Big targets. Alive when idle.
   3D low-poly with painted textures, lighting and shadows - not flat 2D shapes. No assets, no libraries, no network.
3. Prove it headless: `make shot GAME=my-game` (look at the PNG), `make icon GAME=my-game`,
   `make check GAME=my-game` and `make check GAME=my-game SAN=1`. Add `games/my-game/test.touch` (see `docs/testing.md`).
4. Fill in `games/my-game/game.ini` (`docs/manifest.md`) with `tested = desktop` unless you really ran it on the device.
5. Open **one pull request that touches only `games/my-game/`**. Say what a child can do and what you could not verify.

The maintainer reads every line, tries it, and decides which channel it joins (`channels/`). You do not edit
anything outside your game's directory. By submitting you agree your game is released under the license in its manifest.
