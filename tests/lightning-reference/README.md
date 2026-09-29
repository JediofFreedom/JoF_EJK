# Original-binary lightning regression

This test compiles `FX_ForceLightningReference` directly from `fx_force.c` and
checks its direction/timing event stream against 14 frames recorded from the
supplied MBII R22.3.01 x86 routine. It verifies trace endpoints, cached directions,
arc emission and sound calls, random-call ordering and timer state. The recorded
routine is checked with `cg_lightningEnvironmentAngle 360`, which preserves its
unrestricted directions. Eight runs cover every combination of the three optional
pack handles (narrow, wide, arc), including no pack and the full pack. They verify
main-effect selection and axes, fallback branches and surface impacts, and no
duplicate fallback impacts when a pack arc owns its impact effect. Fallback
impacts skip sky/no-impact/nodraw and solid-start traces.
Float comparisons allow 0.001 units for x87/SSE rounding differences.

`lightning_forward_arc_check` exercises 3,853 cases of the same production helper:
fresh and cached directions, left/right angle boundaries, backwards directions,
player yaw/pitch, narrow/wide lightning, hits/misses and cvar bounds. It checks
that traces and emitted bolts stay within the configured cone, redirects preserve
trace reach, and the sound positions and timers follow the accepted traces.

```powershell
cmake -S tests/lightning-reference -B build/lightning-reference-tests -A x64
cmake --build build/lightning-reference-tests --config Release
ctest --test-dir build/lightning-reference-tests -C Release --output-on-failure
```

To independently regenerate the fixture, install `pyelftools` and `unicorn`
in a temporary Python environment and run:

```powershell
python tests/lightning-reference/record_reference.py E:\cgamei386.so
```

The recorder verifies the reference binary's SHA-256, loads its ELF segments
into an x86 emulator and executes `CG_DoLightningArcs` at `0x97dd0`. It replaces
only external random, trace, AngleVectors, effect and sound calls with controlled
callbacks. The fixture is generated from machine code, not from the port.
