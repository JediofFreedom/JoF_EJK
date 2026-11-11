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

### Resource and material follow-up at `81b10be`

The frozen production checkpoint is `81b10be3490a2f08884110717583aaa6e0873e28`,
reviewed against `eebb4d5`; test-only commit `d24c593` changes no
runtime source. [The separate follow-up ledger](followup-81b10be.json) preserves
the earlier `0fcdbdd` audit and GPU history. The target still has 55 compiled C++
files. This delta changes 18 of them and five headers, including a new shared
memory-requirements header. The existing changed `tr_local.h` is also newly
inventoried; its 112-file manifest separates 109 tracked source/
include files from three local developer tools.

The changes honor mandatory dedicated allocations for textures, attachments,
readback and buffers; align SPIR-V only when required; preserve worker completion
after exceptions; and keep optional failed pipeline caches null. Selected
creation/query/map/bind/reset/idle failures now stop before dependent operations.
Readback owns resources across possibly pending submissions and releases them
after completion/device-loss teardown. Material fixes clamp negative entity
animation frames, sanitize fog S/T independently, size stencil-shadow colors for
extruded vertices and restore weather transforms after first-frame caching.
Shared vertex/indirect arenas remain available when requirements match.

There are now **106 focused cases across ten audit suites**: core 29, asset
integrity 12, frontend 13, debug/capture 7, capacity 2, command lifetime 8, resource
memory 17 and material runtime 18. The three expanded/new CTest registrations
were independently counted. Core/resource checks pass x64/x86 Release and x64
ASan; material checks additionally pass x86 ASan. Core before-fix checks produce
14 defect failures/14 passing controls, excluding one deliberately crashing null
allocation baseline. Resource before checks cover five failures/four controls
from three functions, rather than every worker/helper case; material before
checks produce 12 failures/six controls. Services remain stubbed and fatal errors
model nonreturning imports; these are selected contracts, not global recovery.

- Full ordinary Windows x64/x86 builds passed for the frozen 81b10be source; root verified explicit native-command exit 0 for both architectures.
- The complete local Windows runner passed all 66 discovered suites without skips.
- Compatible Clang 22.1.3 analysis completed 55/55 direct contexts with 13 diagnostic contexts retained and no parse errors.
- CI run 43075400446 passed Windows (66/66), Ubuntu (64/64 applicable with two explicit Windows-only skips) and the full native Linux client. macOS failed only material_fog-invalid-t in vulkan-material-runtime (63/64 applicable suites passed); the exact assertion and production-versus-fixture cause are under investigation.
- The six ordinary 81b10be GPU profiles passed (23 map loads, 50 captures), as did weather and the corrected bot-shadow acceptance run (six maps, 33 captures). SSAA/HDR failed real driver validation for refraction blit destination width and dynamic-glow clear rectangles; owned polling aborted the process, with configuration/runtime preserved. Fullscreen/debug cases are unrun, corrections are in progress, and 81b10be will not be deployed.
- TestClient still contains the validated 0fcdbdd build; 81b10be has not been installed.

Extent mismatches and swapchain image lists beyond fixed capacity now produce
controlled errors instead of unsupported sizes or truncated lists; arbitrary
window resizing is not added. Partial initialization/teardown, all-driver/device-
loss/OOM recovery, complete serialized model state and every material/visual
combination remain unproven. Historical 285-finding dispositions stay 46 fixed /
63 provisional / 176 unresolved. Earlier blocked SDL/window bridge and other
quarantined work remain untouched. No new loading/FPS gain is claimed.

### SSAA and portability follow-up at `481800e`

Production and tests are frozen at `481800e92eeec4b1bdfd2500f63e46949769c30b`.
[The final follow-up ledger](followup-481800e.json) preserves failed `81b10be`
GPU/macOS evidence and its successful subsets without relabeling them as final.
The five-file production delta keeps glow extraction images/MSAA, framebuffer
and render area in main-depth pixel space; later blur targets stay capture-based.
Refraction uses its actual destination extent. Reduced viewport/scissor edges
match the active target, clip both bounds and skip empty clears; ordinary main/
portal rendering keeps its direct identity path. Vulkan BUFFER_OFFSET avoids
null-pointer arithmetic. The portable fog fixture keeps actual assertions and
production fog behavior unchanged, including fused/unfused arithmetic controls.

There are **121 focused cases in eleven suites**: the 14 target-extent groups
pass x64/x86 Release and x64 ASan; 19 material groups pass both architectures in
Release/ASan. Original `81b10be` target functions fail 12 groups with two unchanged
controls passing. A three-file intermediate candidate passed VUID checks but
failed the new depth-alignment contract; its pass is not final semantic proof.
See [target extents](../vulkan-target-extents/README.md) for the actual sizing,
MSAA/refraction-scale and odd-resolution matrix. No shaders are rasterized by
those fixtures. Buffer-offset UBSan is conditional on a real compiler/link probe;
a green suite summary alone does not establish instrumentation was enabled.

- Final ordinary Windows x64/x86 builds passed at 481800e; root verified explicit BUILD_EXIT=0 for both architectures.
- The full final local Windows runner passed all 67 discovered suites without skips.
- CI run 43078570667 passed all four jobs: full native Linux client and native Vulkan on Windows/macOS/Ubuntu. Windows passed 67/67 suites without skips; macOS/Ubuntu each passed 65/65 applicable suites plus two explicit Windows-only skips. The Mac material suite passed after the portable fixture correction.
- On the actual 481800e runtime, all six basic profiles plus weather, bot/shadows and SSAA/HDR passed (32 accepted map requests, 92 captures). Fullscreen crashed with access violation 0xC0000005 during the first video restart, without a preceding validation error or forced termination; configuration/runtime hashes remained unchanged. Debug/mips remains unrun, so the complete 11-profile matrix has not passed.
- TestClient remains on validated 0fcdbdd; neither 81b10be nor 481800e is installed.

A compatible Clang 22 pass at `481800e` analyzed the four changed SSAA C++
contexts with zero diagnostics. This is not a new whole-target analysis: the
last full 55-context run is historical `81b10be` with 13 diagnostic contexts.
Prior source hashes remain intact alongside the new 112-file snapshot. A separate
bounded VBO index/byte-protocol follow-up was automatically rejected as possible
cybersecurity work; it remains unimplemented/unverified and was not retried. The
successful BUFFER_OFFSET macro change does not establish that broader protocol. Partial
teardown/global OOM/device-loss recovery, other encoded pointer arithmetic,
complete model state and every visual/material/driver combination remain
unproven. Historical 285-finding triage stays 46/63/176. Blocked/quarantined work
remains untouched, and these correctness changes make no new loading/FPS claim.

### Presentation and fatal-cleanup follow-up at `7bd3470`

Source/tests are frozen at `7bd347034640286a2c1f6720dbd4f3e07049c9a6`.
[The new ledger](followup-7bd3470.json) preserves failed `81b10be`/`481800e`
and older `0fcdbdd` evidence. Supported surface extents now update presentation
resources rather than triggering a fatal error on stale dimensions. FBO render/
capture grids stay independent; direct-render grids update before fresh resources
are created. Aspect bars/filter state are recomputed. Separately, fatal engine
cleanup now shuts down server before client, matching normal quit and retaining
live renderer exports for server model cleanup. This changes no SDL/window bridge.

Focused coverage is **126 Vulkan cases in eleven suites, plus five engine fatal
cases** (131 cases/twelve suites combined). Core has 32 and target extents 16
groups, passing x64/x86 Release and x64 ASan. Original `481800e` WSI functions
fail three relevant groups with 29 controls passing. Actual Com_Error/Com_Quit
fixtures pass five groups on x64/x86 and x64 ASan; before-fix fatal/buildscript/
dedicated paths fail while two recoverable/recursive controls pass. These are
service-backed function fixtures, not exhaustive lifecycle or recovery proof.

The renderer-only candidate used engine `481800e` and three patched Vulkan
files, without the engine shutdown fix. It exited normally across fullscreen
maps/restarts. Its original harness rejection compared a 961x541 FBO capture
against 1024x768 presentation; a complete corrected offline validator passed
using render/capture and mode dimensions separately, with restoration to 961x541.
The original artifact remains unchanged beside a hashed reassessment sidecar.
This is candidate evidence, not an actual final-binary pass.

- Final ordinary Windows x64/x86 builds passed at 7bd3470 with explicit BUILD_EXIT=0 for both architectures.
- The final local Windows runner passed all 68 discovered suites without skips.
- CI run 43082756809 passed all four jobs: full native Linux client and native Vulkan on Windows/macOS/Ubuntu. Windows passed 68/68 suites without skips; macOS/Ubuntu each passed 66/66 applicable suites with two explicit Windows-only skips. Material and fatal-shutdown suites passed on Mac.
- On identical 7bd3470 runtime binaries, 11 of 12 GPU profiles passed: six basic plus weather, bot/shadows, SSAA/HDR and both FBO/direct fullscreen cases (38 accepted map requests, 108 captures). The final debug/mip profile failed Khronos required-handle validation because vkUpdateDescriptorSets received dstSet VK_NULL_HANDLE; owned polling forced an abort and preserved configuration/runtime hashes. No complete matrix pass or deployment is claimed.
- TestClient remains on validated 0fcdbdd. The 81/481 stages were never installed, and 7bd3470 is not deployed.

The 113-file source snapshot adds common.cpp (110 tracked files plus three local
developer tools) and preserves prior source hashes. Current targeted Clang22 analysis completed four contexts (Vulkan init/swapchain
and common.cpp engine/dedicated), with three retaining diagnostics. Existing
common lines and nonreturning-error paths remain qualified. Historical full
analysis is 81 (55 contexts/13 diagnostic contexts); clean SSAA-only analysis
belongs to 481. No whole-project warning-free claim is made. Earlier
blocked SDL/window bridge, unverified VBO index protocol and restricted/quarantined
work remain untouched. Historical 285-finding counts remain 46/63/176. There is
no new loading/FPS claim or exhaustive renderer/driver certification.

### Descriptor lifecycle follow-up at `750ff04`

Source/tests are frozen at `750ff044a1d9f90a623a73d5e674dae43a38e21e`.
[The new ledger](followup-750ff04.json) preserves the failed final `7bd3470`
debug/mip profile and its 11 successful profiles. Only vk_image.cpp changes in
production: bulk sampler refresh skips images with no descriptor yet, retaining
valid mip/non-mip updates. Eventual creation/reuse uses the current filter; the
general update helper's existing API contract remains unchanged.

Focused coverage is **130 Vulkan cases in eleven suites plus five engine cases**
(135/twelve combined). Asset integrity now has 16 groups, passing x64/x86 Release
and x64 ASan. Four new cases compile actual image registration, bulk refresh,
assignment and update functions. Before-fix source fails three pending/completion/
reuse scenarios and passes the valid-only control. Engine/Vulkan/cache services
remain stubbed; actual cache lookup and every worker path are not reproduced.

The debug candidate used engine `7bd3470` plus only the image patch. It passed
three maps/15 captures, normal exit, no validation messages or forced termination,
and preserved configurations/runtime. Developer output identified the legitimate
descriptor-less `menu/splash_16_9` entry. This establishes that observed contract,
not that every unassigned image follows an identical worker path. Candidate and
actual final-runtime validation remain separate.

- Final ordinary Windows x64/x86 builds passed at 750ff04 with explicit BUILD_EXIT=0 for both architectures.
- The final local Windows runner passed all 68 discovered suites without skips.
- CI run 43087229764 passed all four jobs: full native Linux client and native Vulkan on Windows/macOS/Ubuntu. Windows passed 68/68 suites without skips; macOS/Ubuntu each passed 66/66 applicable suites with two explicit Windows-only skips, including expanded asset integrity.
- Fresh 750ff04 GPU tests did not start: the launch guard found an existing user-owned TestClient and left it untouched. The complete 12-profile final-runtime validation is waiting for that client to close. The passed older-engine debug candidate remains separate evidence; no final 750 matrix pass is claimed.
- 750ff04 is staged only: 54 payload and 54 matching source SHA256 checks passed (108 checks), with eight protected configuration hashes unchanged at staging. Nothing was installed; TestClient remains on validated 0fcdbdd and older stages were never installed.

The final private feature harness additionally requires actual owned render-window
minimize/restore while fullscreen in both FBO/direct profiles, with class/title/
HWND/PID evidence. This changes runtime test handling, not blocked SDL source.
The 113-file manifest and prior snapshots stay separate. Current Clang22
analysis completed the single changed vk_image.cpp context with zero diagnostics;
this is not a new whole-target analysis. Historical targeted diagnostics,
sanitizer capability limits and remaining model/driver/error-state gaps
are retained; no whole-project warning-free, complete recovery or new loading/FPS
claim is made. Earlier blocked/unverified/restricted work remains untouched.
