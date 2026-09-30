# Lightning deflection checks

These checks compile the production guard, damage, and prediction cancellation
code against real game headers and math functions. Engine imports are mocked,
so the checks do not need a running game or assets.

```powershell
cmake -S tests/lightning-deflection -B build/lightning-deflection-check
cmake --build build/lightning-deflection-check --config Release
ctest --test-dir build/lightning-deflection-check -C Release --output-on-failure
```

Coverage includes Saber Defense 3 without learned Lightning or Force points,
client prediction retaining a server-confirmed guard when the untransmitted
Saber Defense level is zero in the snapshot,
standing and walking, the 100-degree aiming cone and shooter-relative left/right poses, immediate
attack/run/jump cancellation, damage and electrification, guard expiry, multiple
casters, every non-idle saber move and combat recovery preserving their animation timers,
existing Absorb behavior, and the replicated deflection animation state.

For an in-game check, run a server with the new game module and clients with the
new cgame module. With an ignited saber and Defense 3, stand or walk while aiming
at a Lightning user. The normal lightning beam must remain visually unchanged,
while the defender holds the directional saber-block animation. A tight blue-white
contact flare and short electrical crawls should cling to the middle of the blade;
they must not redirect or replace the beam. Running, attacking, throwing the saber,
jumping, holstering, or turning beyond 50 degrees should release the guard. Try
both left/right poses and verify the same presentation from both client viewpoints.

The player must slow down to walking speed to enter the guard. A player who is
hit while running or swinging can still acquire the guard after settling down,
even while the beam continues. Deflection has no Force cost and does not damage
the caster.

The server uses the normal replicated torso animation to communicate the guard.
Cgame recognizes that animation while a nearby player is actively casting
Lightning, then plays namespaced MBII lightning arc and flare assets at the
defender's blade. No custom entity event or borrowed effect flag is involved;
the normal Lightning beam remains on its original rendering path. The new game,
cgame, and asset modules must be used together.

Install the separately distributed `jof-lightning-deflection-assets.pk3` in
`GameData/EternalJK`. It supplies the `effects/mp/lightning_deflect_mb2` and
`effects/mp/lightning_reference` effects, their namespaced textures and shaders,
and original asset credits. These assets are maintained outside this repository;
repository builds do not include them in `jofclient-assets.pk3`. The separate pack
contains no DLLs and uses the standard Jedi Academy base materials and sounds.
Avoid packaging the same paths in another PK3. Use `cg_lightningEnvironment 2`
to enable the MBII lightning rendering mode.
