# Balloon Party

A sunny party room (patterned wall, windows with a sun, hills and a hot-air balloon, bunting, streamers, wooden floor,
round rug, wrapped presents) full of glossy 3D balloons that float, bob, bump softly and collect at the ceiling.
New ones rise from below, so 10 to 28 are always alive. Everything is procedural: no assets, ~2.5k triangles.

**How a child plays (no text, nothing can go wrong)**
- Touch a balloon: it pops with confetti, a big sparkle, a pop and a bell note. Each colour is one pentatonic note
  (red is the low C, pink the high E; bigger balloons are lower), so pops make little tunes.
- Swipe through balloons: they are batted away (squash-and-stretch, a boing) and drift back by themselves.
- Touch empty air: a puff of sparkles and a soft plink; balloons nearby wobble away.
- Now and then a popped balloon releases a spinning star, heart or music note; touch it for a twinkle.
- Idle: balloons keep floating, the camera sways, and after ~2 s (5 s later in the session) a cartoon hand taps a balloon.

**Rules in code:** a finger that lands on a balloon pops it on that tick; a finger slower than `BAT_SPEED` (250 px/s)
pops each new balloon it runs into, a faster one bats whatever its path crosses; 16 px of forgiveness around balloons.
A contact that does not move for 8 s (resting palm, stuck IR beam) stops counting as activity and its glow fades.

**Check on the real device** (none of this could be tested off the Pi / touch frame)
- Frame time: if `pg_frame_ms()` stays above 12 ms the game stops spawning and retires the oldest balloons (down to 14).
  The check is skipped in headless runs because the software rasteriser makes frames slow there.
- Touch feel: is 250 px/s the right pop/bat boundary for small fingers on the IR frame? Is the 16 px padding enough?
  Do palms or ghost contacts pop too much?
- Audio levels over HDMI: pops 0.58, bell notes 0.46, boing 0.5, plinks 0.2 (all scaled down when many play at once).
- Sunbeams, dust and glows are additive/alpha sprites: check they are visible but not distracting on the real panel.

Files: `src/` (`main.c` touch rules, `balloon.c` simulation and rendering, `room.c`, `fx.c`, `snd.c`, `paint.c`, `util.c`),
`test.touch` (scripted session ending in a 10-finger fuzz), `icon.touch` (poses the icon).
