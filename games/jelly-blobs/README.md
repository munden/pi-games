# Jelly Blobs

Six squishy candy jellies (strawberry, lime, grape, lemon, sky, orange) live in a toy-box playroom: foam-mat floor,
polka-dot walls, a window with a drifting balloon, a toy shelf and bunting. Each blob is a real soft body (16 skin
particles + a belly, position based dynamics: shape matching, a volume/pressure constraint, edge limits, floor/wall/
blob collisions, 2 substeps per tick) drawn as a lit, slightly translucent 3D pillow with a painted jelly texture,
gloss highlights, a contact shadow and a big face. Faces look at the nearest finger, blink, and change expression with
the deformation (smile, "oh" when squished or stretched, grin when bouncing or giggling).

## How a child plays (no reading, nothing can go wrong)
- **Poke** a blob: it squishes, goes "oh!" and giggles. **Touch and drag**: the skin sticks to the finger, stretches
  like dough, droops when lifted, and boings back when let go. **Fling** it: it bounces, hits the walls, piles up.
- **Two fingers on one blob** stretch it. Ten fingers at once are fine. A finger that swipes across a blob picks it up.
- **Tap the mat or the wall**: a sparkle and a bell, and blobs standing nearby hop.
- Left alone the blobs breathe, blink, glance at each other and hop; after ~2 s a friendly glove shows how to poke
  and pull a blob (and comes back every few seconds while nobody touches).
- Sounds are soft and rate limited: squish (pitch by blob size), boing on landings, a bell when blobs bump, a tiny giggle.

## Files
`src/soft.c` physics (pure C, no SDL) / `src/scene.c` room, camera, blob and face drawing / `src/art.c` painted textures
/ `src/main.c` input, behaviour, sound, effects. `test.touch` exercises pokes, lifts, a mat slap, two-finger stretch, a fling
and ends with ten random fingers; `icon.touch` poses the icon.

## For the parent / maintainer on the real device (all untested there: only desktop, headless, was possible)
- Frame time on the Pi Zero 2 W: desktop shows update ~0.03 ms, pg3d ~0.2 ms, ~1.5k triangles (max ~1.7k); check `--fps`.
- Touch feel: grab radius is the blob plus 46 px; check that fingertips and palms pick the blob they cover, and that the
  pull spring (`GRAB_K` in soft.c) feels right with the IR frame's latency and noise.
- Audio levels over HDMI: squish, boing, bell and giggle should be soft; nothing exceeds vol 0.55 before the SDK scaling.
- The camera sways a few pixels (z=0 plane stays ~1:1 with the canvas); confirm it is calm on the big panel.
