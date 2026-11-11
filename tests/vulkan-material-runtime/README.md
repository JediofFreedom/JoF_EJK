This suite compiles the actual `R_BindAnimatedImage` and `RB_CalcFogTexCoords`
functions, extracted at configure time. Engine/GPU services and the surrounding
scene state are stubbed; animation selection and fog math remain production code.

It exercises negative explicit frame indices for looped and one-shot entity
animations, ordinary time animation and cinematic/screen-map/fullbright/single
image controls, and independent fog S/T sanitization. A negative explicit frame
must select the first frame before one-shot selection or signed modulo. Invalid
fog distance must not erase a valid depth coordinate.

The fixture proves these selected material calculations, not every shader or
caller contract. The fog-depth failure uses large finite coordinates that
overflow the depth dot product while distance and eye-plane math stay finite;
ordinary finite coordinates remain a separate unchanged control.

The state cases extract the production stageVars layout, silhouette edge
builder and weather entry point. A full 1,000-vertex shadow retains all 6,000
indices and 2,000 initialized colors without changing the next bundle. First
outside-cache, regular weather and bypass paths check CPU/published MVP state.

Initialization cases extract descriptor/storage/vertex/indirect functions and
include the real result-check/memory-requirements helpers and non-tracker
allocation macros. Vulkan driver calls are stubbed: failure injection covers
every descriptor allocation, map and bind failure in ordinary/dedicated paths;
controls check shared arenas, required dedicated chains and per-buffer differing
requirements. Fatal errors throw in the fixture, matching the engine's nonreturn
contract. This proves safe stopping, not global device-loss/OOM recovery.

The expanded color layout adds 12,000 bytes to the global tessellator and no
per-frame allocation. Reported geometry capacity uses requested buffer bytes,
not padding returned in allocation requirements.

Use `-DVULKAN_SOURCE_DIRECTORY=<saved source directory>` for before-fix controls.

```powershell
cmake -S tests/vulkan-material-runtime -B build/vulkan-material-runtime
cmake --build build/vulkan-material-runtime --config Release
ctest --test-dir build/vulkan-material-runtime -C Release --output-on-failure
```
