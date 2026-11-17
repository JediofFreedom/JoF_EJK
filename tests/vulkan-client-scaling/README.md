# Vulkan client scaling regression

Compiles production registration, resolution-query and 2D draw entry points,
plus the production scene-coordinate conversion block. Window dimensions stay
1920x1080 with SSAA off/on while internal rendering stays 1920x1080/3840x2160.
Checks menu cursor scaling, console line width, full and partial 3D viewports,
adjacent edges with a custom render size, and the existing virtual 640x480
contract for stretch/rotated pictures. No graphics driver is required.

```
cmake -S tests/vulkan-client-scaling -B build-client-scaling
cmake --build build-client-scaling --config Release
ctest --test-dir build-client-scaling -C Release --output-on-failure
```

`EJK_SOURCE_DIR` can select a pre-fix production snapshot as a negative control.
The 0ee764e source fails the SSAA client-dimension assertion; the fixed source
passes. This suite is automatically discovered by project-audit/regressions.py.

Validation: Windows x64/x86 Release Vulkan builds passed; all 16 target-extent
cases passed. An isolated 960x540 live Vulkan run rendered mp/ffa3 with SSAA
on/off, HDR, 4x MSAA, bloom and dynamic glow, including video restarts and normal
shutdown. Both menu/world captures were visually checked for matching layout.
This validates dimensions and drawing; physical mouse feel still needs playtesting.
