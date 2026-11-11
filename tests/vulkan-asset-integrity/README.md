# Vulkan asset integrity regressions

This suite compiles the production texture preparation/filtering, sampler
refresh, sprite-bundle identity, model vertex-input and image replacement
paths. Numerical shader cases compile the actual GLSL case/call statements
with equivalent small C++ scalar/vector types. The regression does not replace
real GPU shader or synchronization validation.

Texture preparation retains the actual `stb_dxt.h` macros rather than replacing
them with safer test definitions. The renderer uses typed `std::max` for new
dimension clamps: stb's branchless `MAX` macro does not parenthesize shift
arguments. A real validation run exposed that integration error; it is covered
by the thin-device-limit tests. Invalid preparation metadata is cleared, and
the immediate producer fixture verifies a failed preparation cannot create or
upload a GPU image.

It checks thin-texture device limits, one-dimensional HQ mip tails and unchanged
two-dimensional HQ output; non-mip descriptors after sampler replacement;
animated sprite hashes and full image arrays; model portal distance and vertex
alpha; original UVs for deformation; every pipeline type across ordinary,
Ghoul2 and MDV layouts; both model draw binders; and old-image replacement after
a successful wait, including preservation after a failed wait.

Configure with `cmake -S tests/vulkan-asset-integrity -B build/vulkan-assets`,
then build and run `ctest --test-dir build/vulkan-assets --output-on-failure`.
`PRODUCTION_ROOT` optionally selects a saved production source tree for
failing-before checks. The ordinary square-filter control passes before the
initial fixes; the nine original defect cases reproduce their corresponding
defects. Two added cases cover invalid dimensions and failure propagation.

Prepared texture cache keys advance to v3 so old HQ mip results are not reused.
The matching shipped SPIR-V was regenerated for all 74 affected model
vertex/refraction variants, including fixed-color/identity, environment and fog
variants. Unrelated shader arrays and binding symbols remain unchanged. Each
regenerated module passed `spirv-val --target-env vulkan1.0`; compiler arguments
and hashes are recorded in the private audit evidence.

Four descriptor-lifecycle cases compile actual `R_AllocImage`,
`vk_texture_mode`, `vk_assign_image_descriptor` and `vk_update_descriptor_set`.
They reproduce the pool's registered-but-unready image state, retain valid
mip/non-mip sampler refresh controls, and verify that later job completion and
resident-image reuse select the current filter. Vulkan descriptor validation,
sampler creation, engine allocation/pool services and imports are stubs; the
resident cache lookup itself and every worker branch are not reproduced.

The `7bd3470` image source fails all three pending-image scenarios with a null
destination descriptor; the valid-only control passes. All 16 suite cases pass
on x64/x86 Release and x64 ASan after the narrow bulk-refresh guard. The ordinary
descriptor update helper still rejects no failures silently; descriptor-less
pool entries receive their descriptors during actual creation/reuse instead.
`DESCRIPTOR_SOURCE_ROOT` can select a saved image source for those four cases
without changing unrelated suite sources. Private before evidence is
`G:/code/JoF_EJK-performance-run/vulkan-followup-descriptor-before.log`.
