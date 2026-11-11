# Shader registration during queued rendering

This harness compiles `FixRenderCommandList` and `SortNewShader` from vanilla,
Rend2 and Vulkan with minimal renderer state, command records and sort-key helpers.
It reproduces a null shader produced when inserting a shader after draw
surfaces have been queued and a stale command after `commands.used` references
the same surfaces. Walking both commands increments the index twice, past the
last registered shader. Vanilla and Rend2 fail against the unbounded command walker.
Vulkan's upstream walker writes a terminator, but its shader remapping reads an
already shifted sorted-shader table and can select the wrong material. It now
shifts queued sort keys directly and uses the same explicit command boundary.

The corrected walker uses `commands.used` as its end. The render-command
terminator is written when submitting a batch, so searching for that marker
while building a batch can reach older commands. Tests cover the shared-surface
case, an empty queue containing old bytes, alignment after a color command,
untouched expired surfaces, preserved entity/fog/light bits and the server-skin
early return.

```powershell
cmake -S tests/renderer-shader-registration -B build/shader-registration -A x64
cmake --build build/shader-registration --config Release
ctest --test-dir build/shader-registration -C Release --output-on-failure
```

The failing-before/passing-after case matches the class of a reported
`RB_BeginSurface` access violation: the dump has a null incoming shader and a
read at address `0xd0`. The dump alone does not establish every step that
produced the bad index. Offline validation also exercised `T1_Fatal` (11,808
faces), the player model grid, cosmetics, a renderer restart and clean exit
with both renderers. That does not reproduce a populated remote server.
