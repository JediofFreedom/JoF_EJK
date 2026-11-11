Native Vulkan audit through code checkpoint `0fcdbdd`, beginning at `4dd25b0`
and compared with `54384fc`. This directory records review coverage and evidence;
it has no CMake target and adds no executable regression suite. The machine
ledger is [coverage.json](coverage.json).

The supplied review ledgers cover all **55 direct C++ sources** in the Vulkan
CMake target, plus 22 project headers, 25 GLSL/template files and five developer
shader tools: 108 unique reviewed entries. The extra reviewed `tr_terrain.cpp`
is absent from the target; its tested correction is not shipped terrain coverage.
Shared image/font/Ghoul2/math/string dependencies retain separate project
coverage. Vendored Khronos headers are not newly audited first-party code.
Per-file hashes identify the supplied review snapshots. A separate 110-entry final
manifest records 107 tracked sources/includes matching the final commit and
three explicitly untracked developer-tool snapshots; initial review hashes
remain intact.

| Area | Confirmed changes | Focused evidence |
| --- | --- | --- |
| Frame/device lifecycle | Explicit acquired-image/submission ownership; invalidate dynamic state after static postprocessing; restore every compatible fog descriptor; stop failed frame-command paths; safe readback bounds/host visibility; supported sparse MSAA and WSI capabilities; remove duplicate instance creation | [Core lifecycle](../vulkan-core-lifecycle): 13 cases on x64/x86 and x64 ASan; baseline failures; actual result/gamma/rounding helpers |
| Image/model interfaces | Thin-texture limits with real stb macros; rectangular HQ mip tails; sampler refresh; complete sprite hashes; original deformation UVs; portal and explicit alpha; last generic enum variants; safe image replacement and failed-preparation outputs | [Asset integrity](../vulkan-asset-integrity): 12 cases on x64/x86 Release/ASan; 11 defect failures before fixes plus unchanged square-HQ control; 192 layout combinations |
| Frontend/Ghoul2 | Patch midpoint LOD/retained height errors; scene/allocation/screen-map bounds; light saturation; bounded model/token copies | [Patches](../vulkan-frontend-patches), [scene](../vulkan-frontend-scene), [shader tokens](../vulkan-frontend-shader-token): 13 cases with baseline failures and x64/x86 ASan; one case is unbuilt terrain |
| Debug/capture | Preserve complete normals/polygons in bounded batches; apply raw-FBO gamma once; use aligned PNG pixel pointer | [Debug/capture](../vulkan-debug-capture): 7 cases on x64/x86 and x64 ASan, all failing before fixes; deliberately misaligned allocation and odd patterned PNG |
| Uniform/fog capacity | Exhaustion cannot publish invalid offsets; bounded growth; upload all 256 legal fog entries using shared 12,304-byte UBO | [Asset capacity](../vulkan-asset-capacity): 2 cases on x64/x86 Release/ASan, both failing before fixes; actual layout/upload/rounding/submission statements |
| One-shot commands | Stop allocate/begin/end/submit/wait failures; free never-submitted or unchanged OOM buffers; retain possibly pending work for pool teardown | [Command lifetime](../vulkan-command-lifetime): 8 cases on x64/x86 and x64 ASan; 7 pre-fix fault failures plus passing success control |

The added postprocessing checks exercise identical-rectangle bloom/glow restoration, changed-rectangle and cached dynamic-only controls, plus fog descriptor restoration and null unused UI descriptors. A 100-draw dynamic-only control retains one pipeline bind, viewport update and scissor update.

These are **55 focused CTest cases in eight new suites**. They compile production
functions or extracted production blocks with documented service stubs; they do
not prove arbitrary model-file/caller/driver states. Initial suspicion of SSAA
double gamma was withdrawn: the existing dedicated-target guard already handled
that path. The new tests preserve it. Raw-FBO checks use the established 8-bit
gamma table and do not establish exact HDR precision/dither fidelity.

The shipped SPIR-V was regenerated and validated with `spirv-val --target-env
vulkan1.0` for 74 model vertex/refraction modules and 29 separate fog fragment
modules: **103 of 194 arrays changed; 91 unrelated arrays and binding symbols
remain unchanged**. Per-module metadata and compiler/validator/source hashes are
recorded in the ledger and private shader evidence. Structural validation does
not establish every material's visual behavior.

Current integration evidence through `0fcdbdd` (versions are kept separate):

GPU checks used a Ryzen 5800X3D and RTX 5070 Ti, native Vulkan 1.4, windowed
961x541 output, software sound and Khronos core/synchronization validation.
They loaded local mp/ffa1, mp/ffa3 and JKSide1 maps in isolated profiles;
fullscreen and other GPU/driver combinations remain unverified.

- Full ordinary Windows x64/x86 builds passed at 0fcdbdd.
- The predecessor `003609f` local Windows runner passed all 64 discovered suites without skips; latest-checkpoint platform counts are recorded separately below.
- CI run 43065663481 passed all four jobs: full native Linux client and native Vulkan on Windows/macOS/Ubuntu. Windows passed 64/64 suites without skips; macOS/Ubuntu each passed 62/62 applicable suites plus two explicit Windows-only skips (sjk-chat and updater).
- Static analysis completed 55/55 direct renderer contexts without parse errors;
  nine contexts retain diagnostics. Original warning dispositions are unchanged.
- Actual final 0fcdbdd direct and FBO/HDR/MSAA cases passed all strict lifecycle checks (six map loads, 12 captures, video restarts, owned minimize/restore, exit 0, zero retained resources and no validation messages). Five additional configurations passed on the immutable scissor candidate; they are separate versioned evidence, not six cases on identical final binaries.
- Validated 0fcdbdd is installed at `E:/Games/testClient/GameData`: launch `Play EJK Dev.cmd` for native Vulkan or `Play EJK Dev - OpenGL.cmd`. All 54 destination SHA256 checks passed, eight existing configuration hashes are unchanged, and overwritten files were backed up (`backup-20261111-171822`).

| Runtime version | GPU cases | Outcome |
| --- | --- | --- |
| Initial `4dd25b0` | Direct | Failed invalid image extents/mip count; corrected in `0f92d64` |
| Corrected `0f92d64` | Direct | Passed |
| `003609f` | Direct; FBO/HDR/MSAA | Direct passed; FBO failed dynamic-scissor VUID 07832 |
| Immutable scissor candidate: `003609f` plus recorded three-file patch | FBO/HDR/MSAA, legacy, texture16, filters/shadows, stress | 5 passed; 20 map loads, 44 captures, 113.82-second stress case |
| Actual final `0fcdbdd` including bloom fog-descriptor restore | Direct, FBO/HDR/MSAA | 2 passed; 6 map loads, 12 captures; strict validation/teardown checks |

Exact runtime hashes and source-diff provenance are in
`vulkan-0fcdbdd-validation-provenance.json`; these runs cover six configurations
across multiple binaries, rather than six cases on identical final binaries.

A corrected earlier `0f92d64` direct GPU case also passed: three maps including
JKSide1, video restart, owned minimize/restore, odd 961x541 captures, normal exit,
zero retained resources and no Khronos validation messages. Root visually
inspected the world/model-menu captures. Lifecycle test duration is not an FPS
measurement; these correctness fixes have no new loading/FPS gain claim.

The initial `4dd25b0` direct GPU case **failed**: new dimension clamps interacted
with the real unparenthesized branchless `MAX` macro from `stb_dxt.h`, producing
invalid image extents/mip count. The initial CPU fixture substituted a safe macro
and missed it. `0f92d64` uses `std::max`, retains real macros in the fixture,
clears invalid output metadata and checks both immediate/file producer results.
The initial failed runtime was stopped manually after owned PID/path/start-time
checks. Its harness `ForcedTermination=false` reflects that external stop and
is not a clean-exit/no-force claim. An earlier harness live-log sharing failure
was also not a renderer pass. The private harness now checks validation output
while polling and aborts immediately; its odd viewport exercises capture padding.

`VK_CHECK` remains a logging helper. Selected explicit failure branches do not
prove other allocation/create/bind/map/pipeline or global teardown/device-loss/
OOM recovery. Dedicated-allocation requirements, SPIR-V byte alignment, complete
MD3/GLM/GLA spans, worker exceptions and every weather/gore/material state retain
explicit gaps. A screen-map `src` analyzer warning has a positive view-count /
non-mutating command-allocation caller-contract explanation; it is not a source
fix or historical finding-count promotion. The nine diagnostic contexts are
`G2_API.cpp`, `tr_backend.cpp`, `tr_bsp.cpp`, `tr_ghoul2.cpp`, `tr_scene.cpp`, `tr_shade_calc.cpp`, `tr_world.cpp`, `vk_shade_geometry.cpp`, `vk_vbo.cpp`; their detailed messages remain in the ledger.

A separate SDL/window bridge follow-up was rejected by automatic approval
review as possible cybersecurity work. It remains unimplemented and was not
retried or rephrased. Earlier blocked botlib/Icarus/loop-sound follow-ups also
remain quarantined. The historical 285 findings remain **46 fixed, 63 provisional
contracts and 176 unresolved**. Review coverage is not an exhaustive safety or
hardware certification.
