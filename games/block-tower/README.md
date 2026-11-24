# Block Tower

Chunky wooden toy blocks in a cosy toy room: stack them, knock them down, fling them, roll a big soft ball into a tower.
Procedural 3D (PS1 style, no assets): painted alphabet/animal blocks, a rug, a window, a shelf of toys, a blue toy chest.

## How a child plays (no reading, nothing can go wrong)
- **Touch a block** and it lifts toward you and follows the finger (it swings and tilts); let go to drop it, flick to throw it.
  Ten fingers hold ten different blocks. Balls can be grabbed and thrown too.
- **Tap the air** and a new random block drops from the sky onto the tapped spot (a chime, stars, a dust puff when it lands).
- **Sweep a finger** through a tower (start on empty space) and the finger shoves everything it meets.
- **The big round button on the blue toy chest** (bottom right) tidies every block into the chest, then a fresh set of
  towers, pyramids, gates and dominoes rains down and settles.
- Left alone: a pointing hand shows where to tap (after 3 s) and demonstrates it; after 20 s a ball is lobbed at the
  tallest tower, and when things have settled the room tidies itself and builds again.

## Limits and safety valves
- At most 40 blocks (the oldest one that is not holding anything up fades away with sparkles), 2 balls, ~60 bodies.
- The room is closed (floor, side walls, ceiling, the chest is solid). Held blocks are speed-capped (no tunnelling).
- If `pg_frame_ms()` stays above 12 ms the cap drops to 28 blocks.

## What to check on the real device (not testable on the dev machine)
- Frame time at 60 fps on the Pi Zero 2 W (about 3.5k triangles, update+cpu3d ~0.3 ms here, so ~3 ms expected).
- Touch feel: pick-up padding is 14 px, grab spring 5 Hz, throw clamp 1300 px/s, button hit radius ~73 px.
- Audio levels: wooden knocks are pitched pentatonic notes (<= 0.5 volume), chimes ring 1.1 s; collisions are rate limited.
- That a flat palm on the screen (many contacts) only drops a few blocks per second and never stalls.
