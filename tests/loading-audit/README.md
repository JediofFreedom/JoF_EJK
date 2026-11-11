# Loading audit regressions

The harness compiles the production archive reopen implementation, filesystem
read resolver, cgame asset catalogue/resolvers, loading redraw policy, Vulkan
model-handle lookup, pixel conversion and image upload functions. Engine and
GPU services are stubbed; archive decompression and JPEG codecs are real.
Release builds keep assertions enabled through the CHECK macro.

```powershell
cmake -S tests/loading-audit -B build/loading-audit -A x64
cmake --build build/loading-audit --config Release
ctest --test-dir build/loading-audit -C Release --output-on-failure
```

Install NASM and put it on PATH to enable x86 SIMD, or pass
`-DCMAKE_ASM_NASM_COMPILER=C:/tools/nasm/nasm.exe`. The checked-in, unmodified
libjpeg-turbo source builds statically without network access. It supports
runtime SIMD detection and a portable fallback when NASM is unavailable.
The upstream codec's licenses are included under `lib/jpeg-turbo` and installed
with the game. This software is based in part on the work of the Independent
JPEG Group.

Coverage:

- Separate cursors/decompressors, concurrent readers, CRCs, failed opens,
  close ordering, ordinary ZIP/ZIP64 and archives with executable prefixes.
- Metadata queries preserve search order, pure restrictions, loose overrides,
  config rules and private-key protection, without allocating a stream.
  The UI retains its old open/close behavior on engines lacking the capability.
- Cosmetic catalogues do no renderer registration; used items load once,
  including legacy fallbacks and missing files. Emoji aliases retain physical
  filenames and load once. Explicit cosmetic commands still resolve models.
- Initial/final loading updates always draw; intermediate bursts are capped
  at 20 redraws/second while retaining the latest status text.
- Model handles are rejected when a map reuses a slot for a different model;
  re-registration updates one entry, including case and separator variants.
- RGB and BGRA conversion buffer sizes, mip offsets, deferred upload copies,
  staging alignment/capacity boundaries, explicit flush and unbatched mode.
- RGB/greyscale, baseline/progressive and odd-sized generated JPEGs; opaque
  RGBA output equals the codec's RGB output. Malformed input frees resources.

The GPU mock checks byte ownership, not Vulkan driver validity. Run real game
map changes, model/cosmetic menus, screenshots and vid_restart as well. Uploads
must flush before frames, staging reuse, immediate submissions and destruction.
Do not enable the separate experimental USE_UPLOAD_QUEUE path for these tests.

`cl_loadTimings 1` prints phase timings and a one-shot connect/map-handoff to
the first active rendered frame. `CL_InitCGame` alone omits the preceding UI
restart. `r_imageUploadBatch 0` provides the prior per-image submission path.
Default batching retains synchronous completion at each batch boundary.

For three controlled whole-client runs (matching engine/module binaries,
isolated profiles, sound enabled and asynchronous map loading disabled):

```powershell
./tests/loading-audit/profile.ps1 -BinaryDirectory C:/build/Release -GameData C:/Games/JediAcademy/GameData -OutputDirectory C:/bench/new
```

It loads T1_Fatal, mp/ffa3, then T1_Fatal again and records each handoff to the
first active rendered frame, plus process time. These are local one-player
loads. The first map includes initial server/map setup that later loads may
reuse; operating-system file caches remain warm. Results are not disk-cold
measurements. Keep the same viewport, installed PK3s and settings between runs.

The investigation covered package indexing/listing/opening, UI startup and
model enumeration, renderer registration, JPEG/PNG decoding and texture
processing, Vulkan transfers/pipeline caching, model handles, cosmetic/emoji
registration, sound loading, first-snapshot presentation and existing timedemo
behavior. A decoded-image cache was measured at 0/512/1024 MiB and discarded:
its source validation, copies and churn did not justify its memory cost.

SJK retains GPU assets and uses worker-based loading in its newer engine.
EJK now retains bounded, independently owned immutable GPU textures across map
changes and rebuilds descriptors; geometry and other map resources still rebuild.
Parallel asset decoding and broader geometry lifetime changes remain separate
architectural work. Network/download delays,
player populations, map complexity and filesystem/GPU caches also affect joins.
These checks do not establish that EJK loads faster than SJK.

## Recorded measurements

Windows Release x64, Ryzen 7 5800X3D / RTX 5070 Ti, 256 installed base PK3s
(about 9 GB), 1230x690 window, sound enabled, both Vulkan VBO paths enabled.
The baseline is `f1f75be`, the previous native Vulkan playtest. Three runs per
version alternate their order and use fresh profiles with matching binaries.
The cycle starts the client, loads T1_Fatal / mp/ffa3 / T1_Fatal, waits 120
frames at 125 FPS after each map and exits. It includes startup and shutdown.

| Measurement, median of three | Previous playtest | This audit |
| --- | ---: | ---: |
| Complete three-map client cycle | 24.179 s | 14.454 s |
| First T1_Fatal, CL_InitCGame only | 4.49 s | 2.58 s |
| mp/ffa3, CL_InitCGame only | 3.20 s | 1.54 s |
| T1_Fatal revisit, CL_InitCGame only | 3.86 s | 2.02 s |

The complete cycle is about 40% shorter. Candidate handoff-to-first-active-frame
medians were 3.807 / 2.354 / 2.809 seconds. The uninstrumented baseline cannot
report that new timer. A separate instrumented pre-audit run recorded
5.570 / 4.651 / 4.918 seconds; those are single-run diagnostics, not medians.

In those diagnostic runs, repeated UI startup fell from roughly 1.54 s to
0.62–0.70 s, eager cosmetic setup from about 0.77 s to milliseconds, and
T1_Fatal immediate GPU transfer submissions from 1,761 to 513. Counters include
buffer/utility transfers, not just textures. JPEG SIMD plus direct RGBA output
reduced aggregate image read/decode time from about 1.97 s to 1.01–1.14 s.
No resolution, texture-quality or sound reduction is used for these gains.

### GPU residency follow-up

The native Vulkan default now retains independently owned immutable textures
across map changes. On the same machine, settings and three-map cycle above,
three alternating pairs used the same new binary with `r_textureCacheMegs`
set to 0 or 512 before renderer startup. Pair order was 0/512, 512/0, 0/512;
no local compilation ran during measurements.

| Measurement, median of three | Cache disabled | 512 MiB cache |
| --- | ---: | ---: |
| Complete three-map client cycle | 13.951 s | 13.433 s |
| First T1_Fatal, handoff to first active frame | 3.586 s | 3.565 s |
| mp/ffa3, handoff to first active frame | 2.275 s | 1.950 s |
| T1_Fatal revisit, handoff to first active frame | 2.795 s | 2.475 s |

Later map loads were approximately 14% and 11% shorter; the whole cycle was
about 4% shorter. The first map was effectively unchanged. These pairs isolate
GPU residency; the earlier baseline comparison measures the broader loading
changes and was collected separately. Neither comparison measures SJK.

Use `profile.ps1 -TextureCacheMegs 0` and `-TextureCacheMegs 512` with fresh
output directories to repeat the comparison. Omit that argument to use the
configured budget. The current default is automatic (`r_textureCacheMegs -1`),
scaling with device capacity/headroom up to 2 GiB on x64, or 128 MiB on x86.
The cache validates source/package identity, pixel-processing settings and
image flags. Loose overrides decode normally, pure restrictions still apply,
and descriptor sets are recreated after map reset. Full video/device restart
releases all retained images and memory before unloading the renderer.

The Vulkan headers are updated to Khronos 1.4.368; instance negotiation
supports older loaders and requests up to API 1.4. This is a native Vulkan
backend with no OpenGL context or OpenGL import callbacks. Header/API version
updates alone are not performance improvements. See the renderer's
[upstream notes](../../codemp/rd-vulkan/UPSTREAM.md) for audited upstream
commits, the adapted model LOD fix and renderer API compatibility requirements.

### EJK Vulkan transfer and synchronization follow-up

The renderer now shares bounded staging storage between textures and static
model/world buffers. It avoids a separate staging allocation, mapping,
submission and queue-idle wait for each model buffer. Core Vulkan 1.3 devices
use synchronization2 barriers/submissions after feature queries; older devices
keep native Vulkan synchronization. Pipeline lookup uses complete semantic keys
instead of linear scans/struct padding, and post-processing pipelines also use
the persistent driver cache. Khronos validation exposed splash transfer usage,
layout preservation, acquisition ordering and command-buffer lifetime issues;
these are fixed.

On the same hardware/data/settings as above, three alternating pairs compare
the deployed `9c240af` renderer with this follow-up. Profiles are fresh, the OS
cache stays warm, and no local compilation or other game client ran during the
measurements. Both variants retain the same 512 MiB texture cache and VBO paths.

| Measurement, median of three | Previous deployed renderer | EJK follow-up |
| --- | ---: | ---: |
| Complete three-map client cycle | 13.163 s | 12.526 s |
| First T1_Fatal, handoff to first active frame | 3.401 s | 3.272 s |
| mp/ffa3, handoff to first active frame | 1.862 s | 1.761 s |
| T1_Fatal revisit, handoff to first active frame | 2.477 s | 2.192 s |

The full cycle is about 5% shorter; map handoffs are about 4%, 5% and 12%
shorter. A matched T1_Fatal diagnostic reduced queue submissions from 513 to 25
while retaining 883 image loads and approximately 652 MiB of image data.
These results do not establish an FPS improvement or a lead over SJK.

The optional Vulkan 1.4 host image-copy path queries format/layout/feature
support and optimal device access. An earlier controlled prototype test found
it slower on this driver (14.178 s staged / 15.038 s host, three paired runs).
It defaults off as `r_hostImageCopy 0`; the prototype comparison preceded shared
static-buffer batching. A newer API is not sufficient evidence of a speed gain.

For real world/model/cosmetic rendering, equip, video restart and map-change
checks, with optional external Khronos core/synchronization validation:

```powershell
./tests/loading-audit/vulkan-smoke.ps1 -BinaryDirectory C:/build/Release -GameData C:/Games/JediAcademy/GameData -OutputDirectory C:/bench/vulkan-check -ValidationLayerDirectory C:/VulkanSDK/1.4.363.1/Bin
```

The script uses an isolated profile and restores its environment settings.
It requires all four screenshots, clean texture-cache teardown and zero
validation errors/warnings. `-Synchronization2 0`, `-UploadBatch 0`,
`-HostImageCopy 1`, `-TextureBits 16` and `-TextureCacheMegs 0` exercise alternative
paths. Use a fresh output directory for every run. The production-function
regressions additionally check old-device feature gates, host-copy failures,
mip offsets, mixed upload bytes, large-buffer boundaries, semantic pipeline
keys/base indices/reset, and synchronization2 submission/barrier translation.

### World texture workers and automatic residency

The project-wide follow-up compares the same prototype binary with
`r_textureWorkers 0` and `-1` (up to four workers), a fixed 2048 MiB residency
budget, and the prepared disk cache disabled. Three alternating pairs used
fresh profiles and warm OS caches, with no build, static analysis or other game
client running. The first pair ran synchronous first; the second reversed it.

| Measurement, median of three | Synchronous decode | Worker decode |
| --- | ---: | ---: |
| First T1_Fatal handoff | 3.601 s | 3.249 s |
| mp/ffa3 handoff | 1.836 s | 1.684 s |
| T1_Fatal revisit | 1.196 s | 1.247 s |
| Complete three-map client cycle | 12.236 s | 12.061 s |

The first two loads were about 10% and 8% shorter. The cached revisit did not
benefit; the complete-cycle difference was small. These are prototype feature
comparisons, not timings of every final project-wide change or of SJK.

A separate three-round, rotated-order experiment used residency budgets of
512, 1024 and 2048 MiB. Map handoff medians were respectively
`[3496,1854,2306]`, `[3495,1833,2270]` and `[3483,1791,1147]` milliseconds.
Complete-cycle medians were 13.221, 13.174 and 11.966 seconds. The larger budget
approximately halved the revisit time; the first load was unchanged. This
motivates the device-scaled automatic budget, with driver memory headroom checks
and reclamation of fragmented idle pages when the budget decreases.

Use `profile.ps1 -TextureWorkers 0` / `-TextureWorkers -1` to isolate decoding,
and explicit `-TextureCacheMegs` values to isolate residency. `-TextureDiskCacheMegs`
is separately controlled and defaults to 0 in the harness. See
[prepared-cache checks](../prepared-texture-cache/README.md) for persistence,
corruption, concurrency and bounded-write coverage.

The shared codec tests cover 8/16-bit RGB/RGBA PNG, palette/transparency,
grayscale, every truncation of fixtures, TGA orientation and packet bounds,
failed writes and insufficient JPEG output capacity. Both normal optimized
builds and AddressSanitizer runs are necessary: identical empty loader stubs can
be folded by the MSVC linker, so the fixtures give loaders distinct behavior.
The worker queue also checks concurrent decoding and joining active work before
input lifetimes end.

Three rotated-order rounds then compared the same world-scope cache prototype
with prepared disk data off, cold, and warm (2048 MiB budgets, automatic workers).
Pipeline data was seeded identically; warm texture data was copied into fresh
profiles before starting the timer. No compilation, analysis or other client
ran during measurements.

| Measurement, median of three | Off | Cold cache | Warm cache |
| --- | ---: | ---: | ---: |
| First T1_Fatal handoff | 3.038 s | 3.164 s | 3.019 s |
| mp/ffa3 handoff | 1.565 s | 1.666 s | 1.488 s |
| T1_Fatal revisit | 1.138 s | 1.121 s | 1.148 s |
| Complete client cycle | 11.751 s | 11.941 s | 11.980 s |

There was no complete-cycle benefit, so prepared caching stays disabled by
default. The subsequent registration expansion includes model/UI textures and
needs its own comparison. The cache's existence is not evidence of a gain.
`-SeedVulkanCacheDirectory` copies known cache data before timing; use identical
pipeline seeds when comparing cache modes.

### Broader registration decoding

The final registration scope also queues model/UI textures after renderer
initialization and drains before backend execution, map-context transitions,
sampler/gamma changes and registration end. Captured per-image settings are
compared byte-for-byte against ordinary preparation over 64 combinations.
Corrupt-primary fixtures verify that a valid alternate image can have different
dimensions. Procedural images, transfer sources and runtime/backend requests
remain immediate.

Three alternating pairs on one binary isolate `r_textureWorkers 0` versus `-1`,
with a fixed 2048 MiB GPU cache and the prepared disk cache off. Fresh profiles,
warm OS caches and a quiet machine were used as above.

| Measurement, median of three | Synchronous | Registration workers |
| --- | ---: | ---: |
| First T1_Fatal handoff | 3.572 s | 2.914 s |
| mp/ffa3 handoff | 1.831 s | 1.567 s |
| T1_Fatal revisit | 1.182 s | 1.227 s |
| Complete client cycle | 12.308 s | 11.408 s |

The first two loads were about 18% and 14% shorter; the complete cycle was about
7% shorter. The cached revisit did not benefit. A diagnostic rendering run
queued 667 textures for the first map instead of the world-only prototype's194.
Worker decode effort is summed elapsed time and can overlap other loading work;
use handoff timings for end-to-end effects. Do not add this percentage to the
separately measured residency or earlier branch improvements.

### Preparation, provider lookup and optional catalogs

Texture greyscale/resampling/mip generation/compression shares the same
algorithms with bounded private worker scratch. GPU resource creation/upload
stays on the main thread. Exact-byte fixtures run 240 cases across four threads,
including HQ/simple filtering, rectangular chains, BC3 and both 16-bit formats.
Oversized jobs drain and use the ordinary path. Wider resampling keeps the
common stack path but supports bounded index buffers and 64-bit fraction math.

Mounted package members have a provider index, while loose directories remain
live and original pure/reference/duplicate rules still run. Optional species
catalogs build on actual customization use. The automatic basic profile uses
current model/skin cvars without scanning all species; actual script-parent
context handles menu `onOpen` before focus changes. A real customization smoke
check verifies catalog creation occurs only after its explicit open marker.

Three rotated-order rounds compare one disabled feature at a time in the same
binary, with 2048 MiB GPU residency, automatic decoder workers, disk cache off,
fresh profiles and warm OS cache. No local compiler, analyzer or other game
client ran during measurements.

| Measurement, median of three | All enabled | Main-thread prep | Linear package lookup | Eager catalog |
| --- | ---: | ---: | ---: | ---: |
| First T1_Fatal handoff | 2.222 s | 2.347 s | 2.348 s | 2.572 s |
| mp/ffa3 handoff | 0.954 s | 1.054 s | 1.011 s | 1.340 s |
| T1_Fatal revisit | 0.707 s | 0.704 s | 0.763 s | 1.089 s |
| Complete client cycle | 9.176 s | 9.349 s | 9.368 s | 10.626 s |

Map UI initialization median was 112 ms with lazy catalogs versus 514 ms eager;
initial startup UI was 421 versus 761 ms. The complete cycle includes fixed
waits and startup work. These feature comparisons are separate from earlier
worker/residency experiments; their percentages must not be added together.
They do not measure SJK or establish an FPS improvement.

Use `-WorkerPrepare 0`, `-FilesystemIndex 0` and `-LazyPlayerSpecies 0` with
`profile.ps1` to reproduce individual comparisons. Corresponding settings are
`r_textureWorkerPrepare`, `fs_lookupIndex` and `ui_lazyPlayerSpecies`.
