# Environmental lightning with vanilla and JoF effects

`cg_lightningEnvironment` now defaults to `2`. This combines vanilla's main
lightning spray with the environment-arc direction and timing logic recovered
from the supplied Movie Battles II R22.3.01 `cgamei386.so`. `0` selects vanilla
lightning alone and `1` selects the existing environmental nests.
An existing saved cvar value remains the user's choice until changed or reset.

## Appearance

The main spray uses the existing `forceLightning` / `forceLightningWide` handles.
In particular, vanilla's wide effect still emits its full fan: 2-4 bolts with
endpoints 500-524 units forward and up to 384 units sideways. There is no added
override for either stock effect.

The independent environment arcs use JoF's existing `effects/mp/lightning_branch`
single-bolt effect. Using a wide effect for each extra arc would multiply the
whole fan at every hit; the main fan is played once instead. Valid contacts
also play JoF's `effects/mp/wall_impact_small` at the traced surface with its
normal. Impacts skip sky, no-impact, nodraw and solid-start contacts.

Mode 2 adds no MBII effect files, shaders or textures. It uses the same vanilla
shaders and existing JoF effects as mode 1. The reference's separate electrocution
body effect is outside this force-power mode.

## Direction and timing

The port follows `CG_Player`'s force-lightning call to `CG_DoLightningArcs`
(ELF address `0x97dd0`, size 1279 bytes). That caller always supplies level 3,
selecting two narrow environment arcs with component spread 0.5 or five wide
environment arcs with spread 0.8. These are additional to the main hand spray.

- Fresh directions add three independent `rand()` offsets without normalizing,
  then trace 350 times that vector, skipping entity `-1` with `MASK_SOLID`.
- Hits cache the requested endpoint, not the surface contact, for a random
  500-1500 ms. Misses leave the cache and timers alone.
- While cached, `origin - cachedEndpoint` is passed to `AngleVectors` twice,
  the second time with roll zero, and traced 200 units. This unusual conversion
  is present in the binary and accounts for the changing/off-axis directions.
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
against captured execution of the supplied x86 machine code, with deterministic
random, trace and math callbacks. It covers hits/misses, both widths, movement,
cached directions, timer boundaries, frame pacing, pause and time reversal.
It separately checks vanilla main-effect selection, unchanged hand axes, JoF
branch selection and surface-impact placement/filtering. The fixture requires
no external binary or Python packages to run.

The visual composition deliberately uses vanilla and JoF effects. Rendering
still needs an in-game check; these tests validate callback behavior.
