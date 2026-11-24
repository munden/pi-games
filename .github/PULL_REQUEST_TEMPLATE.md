## Game
<!-- id, one sentence on what a child does -->

## Checklist (AGENTS.md sections 3-5, 9)
- [ ] Touch only, no reading, nothing can fail, big targets, alive when idle, gentle sound, no flashing
- [ ] 3D low-poly look, painted textures, lighting + shadows (screenshots attached or reviewed from CI artifacts)
- [ ] `make check GAME=<id>` and `make check GAME=<id> SAN=1` pass
- [ ] Budgets: `tris_max`, `update`, `cpu3d` from the smoke output are in the PR text
- [ ] Only `games/<id>/` changed; `game.ini` valid with `tested = desktop`; `icon.bmp` generated
- [ ] No assets, libraries, processes, network, or writes to disk

## Not verified on the real device
<!-- be honest: frame time, touch feel, audio level -->
