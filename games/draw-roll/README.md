# Draw & Roll

Children draw ramps with their fingers; shiny painted balls drop from a friendly cloud, roll and bounce along the
ramps and tumble into a smiling bucket that eats them with a chime. A toy-theatre diorama: painted sky backdrop,
quilted cushion walls, grassy hills that always funnel stray balls back to the bucket.

**How a child plays (no reading, nothing can go wrong)**
- Drag any finger anywhere: a thick, glossy 3D ramp appears under it, in a different bright colour per finger
  (up to ten at once). Balls roll on it the moment it exists. A tap on empty space just makes sparkles and a note.
- Tap the cloud: it pops a ball out. It also drops one by itself every ~2.5 s. The bucket's googly eyes follow the balls.
- Tap the big sponge (bottom-left): every ramp breaks into tumbling chunks and bubbles and washes away.
- Idle: a pointing hand "draws" a rainbow ramp at the start and after ~10 s untouched (it melts away by itself).
- Too many ramps: the oldest ones thin out and fade gradually (limit 240 pieces); recycled balls shrink away with sparkles.

**Sound** (all synthesised, soft, rate limited): a pentatonic bell note when a ball hits a ramp (higher on the screen =
higher note), pop from the cloud, rising chimes when the bucket eats, whoosh for the sponge, quiet blips while drawing.

**Please check on the real device** (desktop-tested only; frame time, touch feel and audio level are unverified there)
- `--fps`: `work=` should stay well below 12 ms with ~240 ramp pieces and 12 balls (scribble with all ten fingers).
  The game lowers its own limits above ~12 ms (fewer pieces/balls/sparkles).
- Drawing feels attached to the finger (the pen smooths the touch by ~1 tick); strokes near the side walls slide along them.
- The sponge washes on release of a short tap, never while a palm rests on it (> 2.5 s ignores it).
- Sound level through the HDMI sink is gentle, nothing startling; hold both top corners still exits (not used by the game).

**Maintainer notes**: sources are in `src/` (`strokes.c` ramps, `balls.c`, `toys.c` cloud/bucket/sponge, `scene.c`
diorama + static physics, `fx.c` sparkles, `hint.c`). Tuning constants (sizes, limits, positions) are at the top of
`util.h`. Physics: pg_phys circles (r 20-25, restitution 0.6) + static segments (ramps r=10, hills, walls), 3 substeps.
