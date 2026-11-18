The fixture compiles the actual Vulkan and vanilla `R_BoxSurfaces_r` collectors
and production scalar plane/vector helpers. `baseline-surfaces.h` freezes the
old collector from `30451bf`. Shader-flag reads, plane tests and normal tests
are counted without changing their values or arithmetic.

Both collectors must preserve the complete output array, surface order, list
length and every surface's `viewCount`. Cases cover duplicate references within
one leaf and across leaves, rejected NOIMPACT/NOMARKS/FOG/back-facing/out-of-box
surfaces, grid/triangle/unsupported types, axial/nonaxial planes, one-sided tree
traversal, full/zero/small capacity, prefilled output, existing markers, and a
fresh projection marker after shader flags change. The capacity check remains
before the early visited check. No state is reused across projection calls:
`R_MarkFragments` advances `tr.viewCount` before entering this collector.

In the duplicate-leaf fixture, exact outputs are retained while shader reads
fall from 70 to 18, plane tests from 20 to 7, and normal tests from 13 to 3.
An old source tree selected with `-DMARKS_SOURCE_DIR=...` passes the output
comparisons but fails the reduced-work assertion. x64 ASan and x86 Release
exercise both actual production collectors.

Optional installed-map inspection takes an extracted JKA BSP, without bundling
game assets:

```
renderer_mark_surfaces --bsp /path/to/maps/mp/duel1.bsp
```

This reads the real BSP record types, reconstructs its leaf-surface references
and face planes, and compares both collectors for 24 conservative shadow-sized
boxes around the recorded crowd positions. For the installed `mp/duel1`, shader
reads were 3400 to 3206 and plane tests 1733 to 1638, with identical lists and
markers. These are offline collector queries using BSP shader flags; live
shader-script overrides and exact runtime trace inputs are outside that count.
The count establishes duplicate work in real map data. It does not establish
an FPS improvement; quiet matched whole-game measurements remain necessary.
