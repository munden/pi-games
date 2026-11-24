# Sand Pour

Rainbow sand falls from your fingertips into a big wooden toy window on a sunny beach.

**What it does.** The sand is a real falling-sand simulation (room for about 25,000 grains, 3x3 px each) lit like
little beads and drawn as one texture between two layers of PS1-style 3D: sky, sea, a sun with a face, a palm tree,
the box's back wall and inner walls behind it, and the wooden frame with toy-block corners, a big button, flowers,
a beach ball and a bucket in front. The camera sways gently so the box has depth. Sand slides, piles up at a natural
angle, splits around coloured pegs and streams out of a candy-striped funnel that trickles by itself when idle.

**How a child plays.** Touch the window: sand pours from the fingertip in that finger's own bright colour (the colour
keeps cycling, so piles get rainbow layers). Drag to draw ridges. Ten fingers or a whole hand all pour at once.
The big red button (bottom left) opens the trapdoor: the floor slides open, the sand rushes out with a whoosh, the
floor closes. Poke the sun, flowers, ball, bucket and palm tree and they giggle. A little hand shows how to pour
if nobody touches for a couple of seconds. Nothing can go wrong; if the box gets too full or a pile reaches the
funnel it sinks slowly through a small trapdoor on its own. Soft "shh" while pouring, a pentatonic chime each time
a pile grows to a new height.

**Parent / maintainer: please check on the real device** (nothing here has been run on the Pi or the touch frame):
- frame time with `--fps` while 10 fingers pour into a nearly full box (target < 12 ms); the sand step is ~0.05 ms,
  relighting ~0.05 ms and the 3D ~0.15 ms on a desktop, so we expect a large margin;
- that the sand texture looks crisp (nearest filtering, 3 px grains) after the SDK's 2x bilinear scale to 1080p;
- the pouring "shh", the chimes and the trapdoor whoosh at a comfortable loudness on the TV/monitor speakers;
- touch feel: the stream starts about 10 px below the touch point; palms or a forearm pour many streams at once;
- the big button is large enough and easy to hit from the easel, and nothing happens in the top-corner exit areas.

Files: `src/sand.c` (automaton, trapdoor, lighting), `src/art.c` (all painted textures and meshes),
`src/main.c` (touch, idle show, sound, 3D layers). No assets, no files written.
