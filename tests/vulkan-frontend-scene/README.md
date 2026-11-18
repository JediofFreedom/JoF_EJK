# Vulkan frontend input and scene regressions

The suite compiles production polygon/entity/light submission and Ghoul2
initialization functions, and the exact backend-allocation and screen-map
command-copy blocks. Renderer services and model setup are stubbed.

Coverage includes invalid polygon counts/pointers, signed arithmetic boundaries,
exact vertex capacity, live fog selection, null/empty entity inputs, matching
point/linear light saturation with unrelated greyscale settings, model-name
limits before allocation/mutation, negative allocation settings, and complete
screen-map command reservation when the queue is nearly full.

The terrain method case covers a legacy source file that is currently absent
from the Vulkan target's source list; it is not a deployed terrain-runtime claim.
Source extraction can target a saved pre-fix directory with
`-DVULKAN_SOURCE_DIRECTORY=<directory>`. The ten tests fail against checkpoint
`54384fc`; fixed production paths pass x64/x86 AddressSanitizer builds.

```powershell
cmake -S tests/vulkan-frontend-scene -B build/vulkan-frontend-scene
cmake --build build/vulkan-frontend-scene --config Release
ctest --test-dir build/vulkan-frontend-scene -C Release --output-on-failure
```

The fixtures establish the listed API and allocation boundaries; they do not
prove arbitrary GLM/GLA model-file offsets or every Ghoul2 caller contract.
