# Vulkan patch LOD regressions

The suite compiles actual `R_StitchPatches`, merged-edge checks and
`R_MovePatchSurfacesToHunk` from the production Vulkan renderer.

Thirty-two seam cases cover both edge directions, horizontal/vertical source
and destination edges, and either side of each patch. They require the inserted
vertex's midpoint LOD error. Reversed edges previously indexed beyond the
source error array instead of using `k-1`.

A separate test relocates a non-square patch into fresh, deliberately nonzero
hunk storage, frees the temporary grid and checks retained vertices and both
independent LOD arrays. The old self-copy left the height error array unfilled.

Both tests fail against `54384fc`; fixed production functions pass x64/x86
AddressSanitizer. Grid insertion is stubbed to record its requested midpoint and
error; this does not replace a visual curved-map check in the actual renderer.

```powershell
cmake -S tests/vulkan-frontend-patches -B build/vulkan-frontend-patches
cmake --build build/vulkan-frontend-patches --config Release
ctest --test-dir build/vulkan-frontend-patches -C Release --output-on-failure
```
