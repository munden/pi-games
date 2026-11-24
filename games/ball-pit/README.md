# Ball Pit

A giant toy-box ball pit: a padded tub in a bright playroom (rainbow arch, bunting, a smiling sun), full of shiny
painted balls (stripes, dots, stars, hearts, daisies, swirls, beach balls, smileys) in many sizes. Real lit, textured
3D drawn with the SDK, physics in one plane (`pg_phys` circles), camera gently swaying. No text, nothing can go wrong.

## How a child plays
- **Touch the pile.** Every finger is a big glowing circle that scoops, shoves and flings balls. A tap makes the balls
  around it hop into the air.
- **Touch empty space.** Three balls drop from the beam, playing a little tune (low notes left, high notes right).
  Hold a finger there to keep pouring slowly.
- Balls bounce, roll, squash and pile up. At 70 balls the oldest one pops out in a puff of stars, so it never fills up.
- Touch the painted sun and it giggles; balls that hit the beam rattle the bunting.
- Left alone: balls rain gently (up to about 20), and a pointing hand shows what to do (first seconds, again after 8 s).

## Files
- `src/main.c`: physics world (every body is made at init and "parked" inside the floor when unused), touch, drops,
  hint hand, scene drawing. `src/paint.c`: all textures (ball patterns are computed on the sphere, so dots stay round).
  `src/build.c`: tiled walls, rainbow arch, bunting flag.
- `test.touch` (smoke test and screenshot, ends with 10 fingers), `icon.touch` (poses the launcher icon).

## To check on the real Pi and touch frame (so far only run headless on a desktop machine: `tested = desktop`)
- Frame time with 70 balls and 10 fingers: on a desktop `update+cpu3d` is about 0.5-1 ms (the Pi is roughly 8x slower)
  and the peak is about 6,800 triangles. Run with `--fps`; above ~12 ms the game fades out the oldest balls on its own.
- Touch feel: finger circle radius is 50 px (about 3 cm). Small jitters (under ~2 px) are ignored so a held finger
  keeps still; check that palms and forearms (extra blobs) are fun, not annoying.
- Audio levels (chimes, wooden taps, boings, pops, all at most 0.5 volume): check on the real HDMI sink.
