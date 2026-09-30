# Lightning deflection checks

These checks compile the production guard, damage, prediction cancellation, and
effects code against real game headers and math functions. Engine imports are
mocked, so the checks do not need a running game or assets.

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
at a Lightning user. The lightning should gather on the middle of the blade,
crackle, light the surrounding character, and scatter harmlessly toward the
attacker's side of the blade. Running, attacking, throwing the saber, jumping,
holstering, or turning beyond 50 degrees should release the guard. Try both
left/right poses, two casters, single/dual/staff sabers, player scaling, spectators,
and `cg_lightningEnvironment 0`. The remaining level-3 fan should stay visible.

The player must slow down before the first hit; a player already being shocked
cannot enter the guard until the existing electrification expires. Deflection
has no Force cost and does not damage the caster.

The update reuses the unused EF2 bit 8 and extends `EV_SABER_BLOCK` with parameter
2, leaving the engine's network field layout and event numbers unchanged. Older
clients can process the block event, but the new cgame is needed for the held
pose's immediate local cancellation and the blade-attached visuals.
