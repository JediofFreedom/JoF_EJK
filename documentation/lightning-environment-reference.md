# Environmental lightning and optional effect packs

`cg_lightningEnvironment` now defaults to `2`. This uses the environment-arc
direction and timing logic recovered
from the supplied Movie Battles II R22.3.01 `cgamei386.so`. `0` selects vanilla
lightning alone and `1` selects the existing environmental nests.
An existing saved cvar value remains the user's choice until changed or reset.

## Appearance

Mode 2 uses these optional effect files when an external asset pack supplies them:

- `effects/mp/lightning_reference/lightning.efx`: narrow main spray.
- `effects/mp/lightning_reference/lightningwide.efx`: wide main spray.
- `effects/mp/lightning_reference/lightning_arc.efx`: environmental bolt,
  including any impact effect declared by the EFX.

Each missing or unregistered handle falls back independently. Main sprays use
the existing vanilla `forceLightning` / `forceLightningWide` effects. Environmental
bolts use JoF's `effects/mp/lightning_branch`, with `effects/mp/wall_impact_small`
at valid traced surfaces. Fallback impacts skip sky, no-impact, nodraw and
solid-start contacts. Pack arcs own their impacts and do not receive an additional
fallback impact. The main spray plays once per invocation with the hand axes.

No additional EFX, textures, shaders, sound files or PK3s are included in this
change. The tested wider spray and custom hand flash, ignition and impact sounds
remain in the separate local asset pack. The usual sound paths are still used:
`sound/weapons/force/lightning` and `sound/weapons/force/lightninghit1` through
`lightninghit3`. An installed pack can override those sounds normally.

## Forward angle

`cg_lightningEnvironmentAngle` is archived and defaults to `360`, preserving
unrestricted reference directions. Set it to `160` to allow environmental bolt
directions to deviate up to 80 degrees from the player's current aim.
Directions outside that cone are redirected straight forward before tracing,
emitting and positioning sounds, preserving the randomized vector length.
This also applies to cached directions after the player turns.

The cvar takes the total cone width in degrees, clamped to 0-360. `0` points all
environmental bolts forward; `360` restores unrestricted reference directions.
Changes apply immediately in mode 2. This controls environmental bolt directions;
main-spray width and the jagged shape of individual bolts remain EFX settings.

## Direction and timing

The port follows `CG_Player`'s force-lightning call to `CG_DoLightningArcs`
(ELF address `0x97dd0`, size 1279 bytes). That caller always supplies level 3,
selecting two narrow environment arcs with component spread 0.5 or five wide
environment arcs with spread 0.8. These are additional to the main hand spray.

- Fresh directions add three independent `rand()` offsets without normalizing,
  then apply the forward-angle limit and trace 350 times that vector, skipping
  entity `-1` with `MASK_SOLID`.
- Hits cache the requested endpoint, not the surface contact, for a random
  500-1500 ms. Misses leave the cache and timers alone.
- While cached, `origin - cachedEndpoint` is passed to `AngleVectors` twice,
  the second time with roll zero. The forward-angle limit is then applied and
  the direction is traced 200 units. This unusual conversion is present in the
  binary and accounts for its changing/off-axis directions.
- Arcs emit when frame time is positive and either at least 50 ms or
  `cg.time % 50 <= cg.frametime`. Main lightning still plays on each invocation.
- Each arc has an independent 500-750 ms sound timer. Sound plays at the
  requested trace endpoint on the owner's `CHAN_AUTO`, with one of three hits.
- Timer comparisons are strict `<`, including the original time-reversal and
  zero-time behavior. Mode 1's budgets, nests and extra hand flash remain specific
  to mode 1.

The source binary SHA-256 is
`1521db9b7bae7d358019c0b72ae240305c801386a0473e0d9506129a32a91fde`.
It matched the official release manifest retrieved on 2026-09-29.

## Validation

`tests/lightning-reference` compares the production direction and timing logic
at angle `360` against captured execution of the supplied x86 machine code, with deterministic
random, trace and math callbacks. It covers hits/misses, both widths, movement,
cached directions, timer boundaries, frame pacing, pause and time reversal.
It checks all eight combinations of optional effect handles, unchanged hand axes,
fallback surface-impact placement/filtering, and absence of duplicate impacts.
Another 3,853 cases verify the angle limit, backward-bolt redirection, cvar bounds,
fresh/cached directions, player turns, and trace/effect/sound consistency.
The fixture requires no external binary or Python packages to run.

Rendering uses the installed engine and asset pack; the automated checks cover
the code's callback behavior rather than the visual appearance of those assets.
