# Block Tower

Chunky wooden toy blocks in a cosy toy room: stack them, knock them down, fling them, roll a big soft ball into a tower.
Procedural PS1-style 3D, no assets: painted letter/number/animal blocks, a rug, a window, a shelf of toys, a blue toy chest.

## How a child plays (no reading, nothing can go wrong)
- **Touch a block**: it lifts toward you and follows the finger (it swings and tilts). Let go to drop it, flick to throw.
  Ten fingers hold ten different blocks. The two big soft balls can be grabbed and thrown too.
- **Tap the air**: a new random block drops from the sky at that spot (chime, stars, a dust puff when it lands).
- **Sweep a finger** through a tower (start on empty space): the finger shoves everything it meets.
- **The big round button on the blue toy chest** (bottom right) tidies every block into the chest, then a fresh set of
  towers, pyramids, gates and dominoes rains down and settles (with a little rising chime).
- Left alone: a pointing hand shows where to tap after 2 s and demonstrates it; after 20 s a ball is lobbed at the
  nearest tower and, once things have settled, the room tidies itself and builds again.

## Limits and safety valves
- At most 40 blocks (the oldest one that holds nothing up fades away with sparkles), 2 balls, about 60 bodies.
- The room is closed (floor, side walls, ceiling; the chest is solid). Held blocks are speed-capped, so none tunnels.
- If `pg_frame_ms()` stays above 12 ms the block cap drops to 28.

## What to check on the real device (not testable on the dev machine)
- Frame time at 60 fps: about 3.5k triangles typical, 4.4k at the stress peak; update + cpu3d is ~0.3 ms here.
- Touch feel: pick-up padding 14 px, grab spring 5 Hz / damping 0.8, throw clamp 1300 px/s, button hit radius ~70 px.
- Audio levels: knocks are pentatonic notes (volume <= 0.5), chimes ring 1.1 s, collision sounds are rate limited.
- A flat palm (many contacts) must only drop a few blocks per second and never stall.
- `icon.touch` poses the scene for `make icon`; `test.touch` is the scripted session for smoke tests and screenshots.
