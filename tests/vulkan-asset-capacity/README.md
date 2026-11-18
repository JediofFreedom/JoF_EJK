# Vulkan uniform and fog capacity regressions

The tests compile the actual uniform reservation/append/publication functions,
the actual sprite descriptor submission statements, the engine's `log2pad`,
the real fog upload function and the shared C++/GLSL fog structure.

Uniform exhaustion must request buffer growth without advancing the mapped
cursor, writing mapped bytes, or publishing/binding an invalid dynamic offset.
Exact-capacity allocations remain valid. Requests requiring growth beyond the
32-bit power-of-two helper's range must fail before entering that helper.

Fog cases cover no world, empty fog lists and 1/16/17/64/256 entries. Every legal
BSP fog reaches the GPU payload unchanged. The shared 256-entry UBO is 12,304 bytes,
below Vulkan's guaranteed 16 KiB uniform buffer range; no fog indices or quality
settings are silently reduced. All 29 affected fog fragment modules were rebuilt
and passed `spirv-val --target-env vulkan1.0`.

Configure with `cmake -S tests/vulkan-asset-capacity -B build/vulkan-capacity`,
then build and run `ctest --test-dir build/vulkan-capacity --output-on-failure`.
`PRODUCTION_ROOT` can select a saved source snapshot; both cases fail before
their production fixes. Real GPU and driver validation remains separate.
