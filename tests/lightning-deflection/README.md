# Lightning deflection checks

These checks compile the production guard, damage, prediction cancellation, and effects code against real game headers and math functions. Engine imports are mocked, so the checks do not need a running game or assets.

```powershell
cmake -S tests/lightning-deflection -B build/lightning-deflection-check
cmake --build build/lightning-deflection-check --config Release
ctest --test-dir build/lightning-deflection-check -C Release --output-on-failure
```

Coverage includes Saber Defense 3 without learned Lightning or Force points,
standing and walking, the 100-degree aiming cone and left/right poses, immediate
attack/run/jump cancellation, damage and electrification, guard expiry, multiple
casters, existing Absorb behavior, animated blade contacts, occlusion, mind trick,
duel isolation, emission limits, continuous contact lighting, and demo rewinds.

For an in-game check, run a server with the new game module and clients with the
new cgame module. With an ignited saber and Defense 3, stand or walk while aiming
at a Lightning user. The normal lightning beam must remain visually unchanged,
while the defender holds the directional saber-block animation. A tight blue-white
contact flare and short electrical crawls should cling to the middle of the blade;
they must not redirect or replace the beam. Running, attacking, throwing the saber,
jumping, holstering, or turning beyond 50 degrees should release the guard. Try
both left/right poses, two casters, single/dual/staff sabers, player scaling,
spectators, and `cg_lightningEnvironment 0`. The remaining level-3 fan should stay visible.

The player must slow down before the first hit; a player already being shocked
cannot enter the guard until the existing electrification expires. Deflection
has no Force cost and does not damage the caster.

The update reuses the unused EF2 bit 8 and extends `EV_SABER_BLOCK` with parameter
2, leaving the engine's network field layout and event numbers unchanged. Older
clients can process the block event, but the new cgame is needed for the held
pose's immediate local cancellation and the blade-attached visuals.

Install the separately distributed `jof-lightning-deflection-assets.pk3` in
`GameData/EternalJK`. It supplies the `effects/mp/lightning_deflect_mb2` and
`effects/mp/lightning_reference` effects, their namespaced textures and shaders,
and original asset credits. These assets are maintained outside this repository;
repository builds do not include them in `jofclient-assets.pk3`. The separate pack
contains no DLLs and uses the standard Jedi Academy base materials and sounds.
Avoid packaging the same paths in another PK3. Use `cg_lightningEnvironment 2`
to enable the MBII lightning rendering mode.
