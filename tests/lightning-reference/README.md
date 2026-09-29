# Original-binary lightning regression

This test compiles `FX_ForceLightningReference` directly from `fx_force.c` and
checks its event stream against 14 frames recorded from the supplied MBII
R22.3.01 x86 routine. It verifies trace endpoints, cached directions, effect and
sound calls, random-call ordering, timer state and main-effect selection.
Float comparisons allow 0.001 units for x87/SSE rounding differences.

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
