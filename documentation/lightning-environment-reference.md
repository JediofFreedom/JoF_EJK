# Reference lightning mode

Set `cg_lightningEnvironment 2` to use the lightning behavior recovered from
the supplied Movie Battles II R22.3.01 `cgamei386.so`. `0` retains vanilla
lightning and `1` retains the existing environmental nests; the default is `1`.
Install the updated `jofclient-assets.pk3` alongside the client game module.

The port follows `CG_Player`'s force-lightning call to `CG_DoLightningArcs`
(ELF address `0x97dd0`, size 1279 bytes). That caller always supplies level 3,
selecting two narrow arcs with component spread 0.5 or five wide arcs with
spread 0.8. The separate electrocution-body call is outside this force-power mode.

- Fresh directions add three independent `rand()` offsets without normalizing,
  then trace 350 times that vector, skipping entity `-1` with `MASK_SOLID`.
- Only `fraction < 1` matters. Hits cache the requested endpoint, not the surface
  contact, for a random 500–1500 ms. Misses leave the cache and timers alone.
- While cached, `origin - cachedEndpoint` is passed to `AngleVectors` twice,
  the second time with roll zero, and traced 200 units. This unusual conversion
  is present in the binary and accounts for the changing/off-axis directions.
- Arcs emit when frame time is positive and either at least 50 ms or
  `cg.time % 50 <= cg.frametime`. Main lightning still plays on each invocation.
- Each arc has an independent 500–750 ms sound timer. Sound plays at the
  requested trace endpoint on the owner's `CHAN_AUTO`, with one of three hits.
- Timer comparisons are strict `<`, including the original time-reversal and
  zero-time behavior. Mode 1's budgets, nest system and extra hand flash are not
  part of mode 2.

The arc effect performs its own endpoint trace and invokes the original impact
effect, including sparks, smoke, small electricity and spark sounds. The flare
asset is registered but not used by the recovered force-lightning call path,
so it is not added to this mode.

## Sources

The supplied binary SHA-256 is
`1521db9b7bae7d358019c0b72ae240305c801386a0473e0d9506129a32a91fde`.
It matches the [official release manifest](https://patcher.moviebattles.org/static/Manifest/MB2Core.xml)
retrieved on 2026-09-29. The matching release archives were used, rather than the
older local MBII installation:

| Archive | SHA-256 from the release manifest |
| --- | --- |
| MB_Effects.pk3 | `418072a785d33f78a2adf6346d4ffe0600e0dd2012fd6b43b8038cb9cd635d1d` |
| MB_Effects2.pk3 | `cbf84daa0d4bc514bdc445a9a06d31dfccad62d50246e9279b82e35e1a401de2` |
| MBAssets2.pk3 | `1787e868e27e8bf703084ad960d3cef108126c7adfe8a13020a20ad05bfd2e5d` |

The first two archives were downloaded and hash-verified. The lightning-flash
texture was read from MBAssets2 using HTTP byte ranges with ZIP CRC verification;
its SHA-256 is `82977b1e98f3c5e55696e6541675e575ffdbd91b41f8e24f16018de189b64248`.
The four effect definitions also match the [official text assets](https://github.com/MBII/TextAssets/tree/ca3f32e41faf43851c19d146182d8f93f0de003a/MB_Effects2/effects/Force)
apart from namespaced dependency paths and whitespace. Namespacing keeps these
materials and textures from altering the other lightning modes.

## Validation

`tests/lightning-reference` compares the production helper against captured
execution of the supplied x86 machine code, using deterministic random, trace,
math and renderer callbacks. It covers hits/misses, both widths, movement,
cached directions, timer boundaries, frame pacing, pause and time reversal.
The fixture requires no external binary or Python packages to run.

Rendering still uses the host engine's effect system. A visual comparison in
the game is needed to assess renderer-specific differences.
