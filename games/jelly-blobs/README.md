# Jelly Blobs

Six squishy candy jellies (strawberry, lime, grape, lemon, sky, orange) live in a toy-box playroom: foam-mat floor,
polka-dot walls, a window with a drifting balloon, a toy shelf and bunting. Each blob is a real soft body (16 skin
particles + a belly; position based dynamics with shape matching, a volume/pressure constraint, edge limits, skin
smoothing, floor/wall/ceiling and blob-vs-blob collisions, 2 substeps per tick) drawn as a lit, slightly translucent
3D pillow with a painted jelly texture, a gloss highlight, a contact shadow and a big face. Faces look at the nearest
finger, blink, and change with the deformation (smile; "oh" when squished, stretched or falling; grin when bouncing).

## How a child plays (no reading, nothing can go wrong)
- **Poke** a blob: it squishes, goes "oh!" and giggles. **Touch and drag**: the skin sticks to the finger, stretches
  like dough, droops when lifted and boings back when let go. **Fling** it: it bounces, hits the walls, piles up.
- **Two fingers on one blob** stretch it. Ten fingers at once are fine. A finger that swipes across a blob picks it up.
- **Tap the mat or the wall**: a sparkle and a bell, and blobs standing nearby hop.
- Left alone the blobs breathe, blink, glance at each other and hop; after ~2 s a friendly glove shows how to take
  hold of a blob and lift it (it returns every few seconds while nobody touches anything).
- Sounds are soft and rate limited: squish (pitch by blob size), boing on landings, a bell when blobs bump, a giggle.

## Files
`src/soft.c` physics (pure C, no SDL) / `src/scene.c` room, camera, blob and face drawing / `src/art.c` painted
textures / `src/main.c` input, behaviour, sound, effects. `test.touch` pokes, lifts, slaps the mat, stretches with two
fingers, flings, then ends with ten random fingers; `icon.touch` poses the icon.

## For the parent / maintainer on the real device (only desktop and headless were possible: nothing here ran on the Pi)
- Frame time: desktop shows update ~0.03 ms, pg3d ~0.15 ms, ~1.5k triangles (max ~1.7k); check with `--fps`.
- Touch feel: grab reach is the blob plus 46 px; check that fingers and palms pick the blob they cover and that the
  pull spring (`GRAB_K` in soft.c) feels right with the IR frame's noise and latency.
- Audio over HDMI: squish, boing, bell and giggle should be soft (every `vol` is <= 0.55 before the SDK's scaling).
- The camera sways a few pixels (the z=0 plane stays ~1:1 with the canvas); confirm it is calm on the big panel.
