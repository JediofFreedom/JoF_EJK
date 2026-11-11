# Native Vulkan regression checks

Build the production pipeline-cache format checks and the extracted engine
renderer shutdown function without requiring a Vulkan runtime:

```powershell
cmake -S tests/vulkan-pipeline-cache -B build/vulkan-check -A x64
cmake --build build/vulkan-check --config Release
ctest --test-dir build/vulkan-check -C Release --output-on-failure
```

The cache test checks every truncated length and a mutation of every byte,
GPU/vendor/driver/UUID changes, pointer width, size limits and complete valid
payloads. The shutdown test verifies that device destruction precedes DLL
unload on normal shutdown and same-renderer Vulkan restarts. GL renderers keep
their existing lightweight restart behavior.

Run the native backend on a real GPU, using a new isolated home directory:

```powershell
./tests/vulkan-pipeline-cache/smoke.ps1 `
  -BinaryDirectory G:/code/JoF_EJK-performance-build `
  -GameData E:/Games/testClient/GameData `
  -OutputDirectory G:/code/JoF_EJK-performance-run/vulkan-validation
```

This loads `T1_Fatal`, exercises both geometry-buffer paths, model thumbnails,
the cosmetics preview and `vid_restart`, then exits. It repeats for cold, warm,
corrupted and disabled caches, requiring native Vulkan logs, device teardown
on restart and cache seeding at the expected points. The engine's renderer
fallback is disabled so OpenGL cannot silently satisfy the check. Screenshots
and separate phase logs are saved in the output directory. The game assets
are read from the installation; settings/log/cache writes use the test home.

Windows Release x86/x64 builds and these tests passed, including x64
AddressSanitizer. The GPU smoke passed on an RTX 5070 Ti (Vulkan driver API
1.4.357); its cache was 687,686 bytes, loaded on video restart and the next
process launch. Corruption was rejected before passing data to Vulkan.
The populated thumbnail grid, cosmetic preview and world were inspected.
These are correctness/stability checks, not loading-speed or populated-server
benchmarks. Linux/macOS source builds are not locally verified.

## Timedemo comparison

`timedemo.ps1 -BinaryDirectory <build> -GameData <installation> -OutputDirectory
<new-test-home> -Record` records a rotating third-person view on offline
`T1_Fatal`. Run the same command without `-Record` to replay it with vanilla
and the four Vulkan world/model buffer combinations. Three runs per variant
alternate forward/reverse order; logs and JSON retain the individual results.
This requires the player's installed map/assets; no demo or assets are bundled.

On Ryzen 7 5800X3D / RTX 5070 Ti, Windows x64 Release, the 401-frame replay at
1920x1080 produced the following median FPS from three runs:

| Renderer | Median FPS |
| --- | ---: |
| Vanilla | 582.0 |
| Vulkan, streaming geometry | 639.6 |
| Vulkan, world buffers | 734.4 |
| Vulkan, model buffers | 659.5 |
| Vulkan, world + model buffers | 794.1 |

This scene is about 36% faster with both Vulkan buffer paths than vanilla.
Individual buffered runs were 794.1, 670.6 and 818.4 FPS, so the result has
meaningful run-to-run variation. The test uses immediate presentation, full
texture resolution, 16x anisotropy, no MSAA/glow/flares, and Vulkan's legacy
dynamic-light mode to match vanilla. It contains one player and does not
represent a populated server, loading time, or every Vulkan visual setting.
Initial shorter tests with unmatched lighting/flares were slower on Vulkan;
those do not support a blanket renderer speedup claim. The test launcher enables
the measured buffer paths but leaves the player's visual settings selectable.
