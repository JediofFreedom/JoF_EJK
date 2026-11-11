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
changes and rebuilds descriptors. Its registration workers decode textures and
prepare mip chains; geometry and other map resources still rebuild. Broader
parallel world/material preparation and geometry lifetime changes remain
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

### Checkpoint versus the previous deployed build

Three alternating pairs compare the previous deployed `6955dca` binary with the
source finalized as `af047b2`. Both use the same installed GameData, window,
quality and sound settings, fresh profiles and warm OS caches. The old build
uses its 512 MiB GPU cache; the new build uses its device-scaled automatic budget
(about 2 GiB here), workers/preparation/index/lazy catalogs enabled, disk cache
off. No local compiler, analyzer or other game client ran during measurements.

| Measurement, median of three | Previous deployed build | Checkpoint |
| --- | ---: | ---: |
| First T1_Fatal handoff | 3.535 s | 2.210 s |
| mp/ffa3 handoff | 1.884 s | 0.948 s |
| T1_Fatal revisit | 2.287 s | 0.733 s |
| Complete client cycle | 13.205 s | 9.089 s |

The handoffs are about 37%, 50% and 68% shorter; the complete cycle is about 31%
shorter. The complete cycle includes fixed waits and startup work. This measures
the combined checkpoint behavior, not isolated features. It remains a local
one-player comparison and does not establish a lead over SJK or an FPS gain.

### Archive reading and restart experiments

MiniZip now reads 2/4/8-byte fields with one callback on ordinary reads, retaining
the original continuation behavior on short reads and errors. A separate
same-toolchain Release benchmark traverses 256 installed Base PK3s with 75,658
members, requesting metadata once per entry as the engine does. After warmup,
seven alternating pairs measured 339.528 ms versus 276.504 ms median: 18.56%
less parser time, with callbacks falling from 7,371,148 to 2,686,534. Metadata,
names, comments, extras, offsets and bytes match. Both versions also extracted
14,451,732,062 bytes with matching sizes/CRCs and zero failures. This measures
archive parsing, not complete startup or joining. See `tests/minizip-directory`.

`fs_zipCatalogFast 1` additionally combines archive navigation and filename/info
retrieval in one call to the existing MiniZip parser. Generic MiniZip APIs remain
unchanged. The new engine-only iterator invalidates its current file on any
combined parse error, including a filename read failure; FS rejects/closes that
package without retrying from the failed iterator. Explicit `0` selects a fresh
legacy scan. This changes no archive priority, purity, references or asset quality.

The [catalogue fixture](../filesystem-catalog-iterator/README.md) passes 309 ZIP
cases on x64 with AddressSanitizer and on x86 Release, including partial reads,
EOF/errors, ZIP64/SFX, private state/cursor/offsets and payload CRCs. All 256
installed packages / 75,658 members have equal names, sizes, CRCs, catalogue
order, regular/pure checksums, duplicate winners and reference behavior.
Catalogue parses fall from 151,572 to 75,914, read callbacks from 2,653,276 to
1,375,934 and seeks from 269,197 to 126,876. These are work counts, separate
from startup measurements.

Five alternating whole-client pairs followed one excluded priming pair, using
the same Release/LTO binary with only this cvar changed. Assets, quality,
pipeline seeds, fresh profiles and warm OS caches matched; texture residency
was 2048 MiB, `fs_restartCache 1` and `r_textureSignaturePrefix 1`. No compiler,
analyzer or other game client ran during timing. The actual measured round
order was:

| Round | Initial FS, legacy ms | Initial FS, fused ms | Complete cycle, legacy s | Complete cycle, fused s |
| --- | ---: | ---: | ---: | ---: |
| 1 | 315 | 219 | 8.1650352 | 7.7436499 |
| 2 | 319 | 227 | 8.0068440 | 7.5277146 |
| 3 | 318 | 221 | 7.9175396 | 7.5329079 |
| 4 | 320 | 225 | 7.9187953 | 7.5510236 |
| 5 | 318 | 222 | 8.1457708 | 7.5266485 |
| Median | 318 | 222 | 8.0068440 | 7.5329079 |

The initial filesystem phase is about 30.2% shorter; the complete T1_Fatal /
mp/ffa3 / T1_Fatal client cycle is about 5.92% shorter, with both improving in
all five pairs. The latter includes startup, fixed waits and shutdown. This
supports default `1`; it is not a 30% whole-game improvement or an FPS result,
and must not be compounded with earlier percentages. `profile.ps1` accepts
`-ZipCatalogFast 0/1`. Raw results are retained locally in
`project-catalog-runtime-benchmark-v2/results.json`; the earlier run is excluded
for a harness-label mismatch, not a game failure.

`fs_restartCache 1`, set before initial startup, retains unchanged
archive catalogs across checksum-feed-only restarts. Existing shutdown/startup,
directory rescans, handle cleanup, package order and pure checks remain in use.
Strong file identity/change stamps and entry counts gate reuse; changed/new
archives take ordinary parsing. Explicit restarts and effective mount changes
rebuild normally. It now defaults to `1`; explicit `0` remains available. The
production-path tests cover replacements with identical sizes/timestamps,
checksum byte equivalence, reference flags, duplicates, races and error cleanup.
Three alternating real-server pairs measured a median restart of 304 ms with
reuse off versus 102 ms on, reusing 262 archives/78,826 members. Initial startup
was 320 versus 325 ms; connect-to-first-present was 5,724 versus 5,492 ms. The
join result includes variable server/network work and applies to this server.

`r_textureSignaturePrefix 1` validates texture candidates only
through the successfully decoded winner. Higher-priority candidates remain
dependencies, including corrupt files that required a fallback. This defaults
to `1` after five additional alternating pairs, following one unmeasured warmup
pair and seeding identical pipeline data. In the same Release/LTO binary, with
2048 MiB residency, archive reuse on and fresh profiles, median handoffs were
1,920/849/635 ms off versus 1,841/790/569 ms on; complete cycles were 8.085 versus
7.911 s. These are about 4%, 7% and 10% shorter handoffs, separate from earlier
branch results. `0` preserves full signatures. `profile.ps1` accepts
`-TextureSignaturePrefix 0/1` and `-FilesystemRestartCache 0/1` for comparisons.

Loading logs now split source metadata from encoded reads and record connection
milestones from setup through challenge, gamestate, cgame readiness and first
presentation. MOTD and target-name resolution have separate durations. Network
payloads, rates and settings are unchanged by these measurements.

### Portable zlib-ng backend

`UseZlibNG` defaults to `ON`. With `UseInternalZlib=ON`, supported compilers and
CMake 3.14 or newer select the pinned zlib-ng 2.3.3 static library in its
zlib-compatible API mode. `-DUseZlibNG=OFF` retains classic bundled zlib 1.3.1;
`-DUseInternalZlib=OFF` continues to select the external zlib package. CMake
3.10-3.13 and unsupported compilers explicitly fall back to classic zlib, so the
project's minimum CMake version remains 3.10.
macOS universal builds with multiple distinct `CMAKE_OSX_ARCHITECTURES` also
use classic zlib because upstream selects optimized sources for only the first
architecture. Single-architecture builds remain eligible for zlib-ng.

Runtime CPU detection and generic fallbacks are enabled. Host-native ISA
selection is disabled. zlib-ng's own IPO/LTO is explicitly disabled in every
configuration to preserve the separation of runtime-dispatched instruction
sets; the engine and its other libraries retain their Release LTO settings.
The modified bundled minizip and bundled PNG code remain in use. No additional
runtime DLL is required. The preserved license and upstream record are installed
under the game's `licenses/zlib-ng` directory; see the
[pinned source and license notes](../../lib/zlib-ng/EJK-UPSTREAM.md).

Three alternating, warmed library pairs compared the same MSVC x64 Release/LTO
harness, modified minizip and production PNG decoder, replacing only zlib. The
ZIP workload expanded 824,071,333 bytes across 8,377 members; the PNG workload
decoded 128 installed images to 101,777,408 RGBA bytes. Each PNG trial reports
the median of five decode passes; the table reports the median of three trials.

| Library measurement, median of three | Classic zlib | zlib-ng |
| --- | ---: | ---: |
| ZIP streaming extraction, including CRC checks | 1,651.93 ms | 1,309.80 ms |
| Production PNG decode | 455.180 ms | 360.055 ms |

These isolated workloads took about 20.7% and 20.9% less time. They do not
measure complete game loading. A separate compatibility pass streamed all 260
installed archives, 77,242 members and 14,501,374,289 bytes with matching CRCs
and aggregate SHA-256; the 128 PNGs also produced matching dimensions and RGBA
bytes. The [backend regressions](../zlib-backend/README.md) additionally exercise
both backends, malformed streams/PNGs, allocator failures and build policy.

Whole-client comparisons used two clean builds from the same `383785a` source,
matching Release/LTO settings, bundled minizip/PNG, runtime DLLs, asset containers
and pipeline seeds. The local T1_Fatal / mp/ffa3 / T1_Fatal comparison used five
alternating pairs after one unmeasured warmup pair, fresh profiles, warm OS
caches, 2048 MiB GPU residency, archive restart reuse and texture signature
prefixes enabled. No compiler, analyzer or other client ran during timing.

| Local measurement, median of five | Classic zlib | zlib-ng |
| --- | ---: | ---: |
| First T1_Fatal handoff | 2,009 ms | 1,980 ms |
| mp/ffa3 handoff | 828 ms | 815 ms |
| T1_Fatal revisit | 602 ms | 576 ms |
| Complete three-map cycle | 8.228 s | 8.225 s |

The complete local cycle was effectively unchanged. The first two handoff
differences were small and noisy; faster isolated decoding does not guarantee
an equivalent reduction in the loading critical path.

Three alternating pairs then joined `13.140.233.106` on the heavier custom map
`yavin_temple_mp2`, with matching assets and settings.

| Real-server measurement, median of three | Classic zlib | zlib-ng |
| --- | ---: | ---: |
| Cgame initialization | 2,465 ms | 2,276 ms |
| Graphics initialization within cgame | 1,743 ms | 1,608 ms |
| Connect to first rendered frame | 5,408 ms | 5,202 ms |

Cgame and graphics initialization were about 7.7% shorter, with all three
paired cgame loads improving. Connect-to-first-frame was about 3.8% shorter;
gamestate arrival was effectively unchanged (2,454 versus 2,458 ms). Network and
server variation still affect the complete join. This supports the new default
for this asset-heavy workload, rather than a claim of 21% faster whole-game
loading, an FPS improvement or a lead over SJK. These experiments are separate
from earlier branch comparisons; their percentages must not be added together.

Recorded result files are `zlib-library-pairs-results.json`,
`project-zlib-client-benchmark/results.json` and
`project-remote-zlib-benchmark/results.json` in the local performance-run output.

### Shader catalogue reuse, live metadata probes and PCM loading

The `189456f` loading follow-up adds three defaults, each with an explicit `0`
control for comparisons:

- `fs_metadataProbe 1` uses a live native Windows missing-file query before CRT
  stream setup for absent loose-file metadata candidates. It keeps existing
  files and uncertain/path-sensitive cases on the ordinary path, without caching
  directory contents or hiding subsequent loose overrides.
- `r_shaderTextCache 1` retains one compressed shader-source catalogue and hash
  offsets. Each reuse obtains the fresh ordered file list and validates its
  winning immutable source identities before rebuilding pointers in the current
  hunk. It preserves duplicate precedence and package reference rules. Loose
  sources, unknown identities, journal modes and read failures reload normally;
  full renderer shutdown frees retention.
- `s_resampleFast 1` avoids phase arithmetic for exact equal-rate and twofold PCM
  output conversion. Sample values, output lengths, volume ranges and other-rate
  behavior match the ordinary resampler; audio quality and effects are unchanged.

Five alternating same-binary pairs after one excluded warmup pair compared all
three controls at `0` versus all three at `1`. Both sides used fresh profiles,
the same pipeline seed, installed assets, 1230x690 viewport, quality settings,
software sound (`s_useOpenAL 0`), 2048 MiB GPU texture retention and disabled
prepared-texture disk cache. OS/driver caches were warm; no competing build,
analyzer or game client ran during timing. The map order was T1_Fatal / mp/ffa3 /
T1_Fatal. These settings compare the new work with the same previously optimized
EJK engine, not with an unmodified retail client or SJK.

| Measurement, median of five | All three off | All three on |
| --- | ---: | ---: |
| First T1_Fatal handoff | 1,891 ms | 1,725 ms |
| mp/ffa3 handoff | 823 ms | 725 ms |
| T1_Fatal revisit | 555 ms | 464 ms |
| Complete three-map cycle | 7.699 s | 7.474 s |
| Texture metadata, first / second / revisit | 97.008 / 99.573 / 107.203 ms | 62.729 / 63.853 / 68.764 ms |
| Sound registration, first / second / revisit | 390 / 93 / 20 ms | 365 / 83 / 17 ms |
| Renderer registration, startup / first / second / revisit | 596 / 78 / 75 / 80 ms | 580 / 25 / 26 / 25 ms |

The map-handoff medians were about 8.8%, 11.9% and 16.4% shorter; the complete
cycle median was about 2.9% shorter. All five first-map and revisit handoffs
improved; four of five second-map handoffs and complete cycles improved. The
fifth enabled cycle was slower overall (7.913 versus 7.699 s), despite its faster
individual map handoffs. Complete cycles include startup, fixed waits and
shutdown, so the map figures do not imply an equivalent whole-process gain.
The reused shader catalogue covered 762 files / 3.28 MiB and took about 18–24 ms.
Stage timings identify avoided work; they overlap the load and must not be added
to the handoff savings or combined with earlier experiments' percentages.

Three additional rotated rounds disabled one new control at a time. Their map
medians were 1745/716/459 ms with all enabled, 1777/750/502 with the metadata probe
off, 1792/760/506 with catalogue reuse off, and 1813/726/465 with the PCM path off.
The first trial created the pipeline seed, so these rounds are exploratory
component evidence, not a fully primed whole-cycle proof or independent effect
sizes to add together. The five-pair comparison above is the primary result.

Raw local evidence is `asset-loading-paired-1/results.json` and
`asset-loading-knockouts-1/results.json` in the performance-run output. Focused
fixtures exercise the production filesystem, resampler and catalogue paths;
the new catalogue fixture preserves exact text, hash order and references across
fresh hunk allocations on x64/x86 with AddressSanitizer. The final `189456f`
Windows checkpoint passed normal full x64/x86 production builds and all 56 x64
standalone suites, with no failures or skips. Native Vulkan core/synchronization
validation passed visible world/models, basic/custom character and cosmetic
menus, video restart, map changes and zero retained resources at teardown.
The refreshed playtest preserved the user configuration; the packaged game/UI
smoke reached an active frame and normal exit. CI run `42999586254` has passed
the full native Linux client without OpenGL and Vulkan module builds on
Windows/Linux/macOS. The complete run succeeded: Windows passed all 56 suites
with no skips; Linux/macOS each passed all 54 applicable suites with two explicit
Windows-only skips.
This experiment establishes an EJK improvement on this workload; it does not
establish loading parity with SJK, a faster SJK comparison or an FPS gain.

A separate `local-sjk-ejk-reference-1` attempt stopped during warmup because the
SJK content selection was unverified and the private harness could not complete
cleanup. It produced no measured pairs or usable parity/winner result; all owned
processes were subsequently cleaned up and original configurations preserved.


### Native Vulkan correctness audit through `0fcdbdd`

The renderer review covers all 55 direct compiled C++ files, with separate
inventories for headers, shaders/tools and unbuilt terrain. Reproduced fixes
cover frame/image/command ownership, image preparation/mips/samplers, model-shader
interfaces, patch/scene/input bounds, bounded debug geometry, capture alignment/
gamma and uniform/fog capacities. There are 55 focused cases in eight new suites,
including saved-baseline failures and x64/x86 ASan coverage with documented
fixture limits. In total, 103 affected SPIR-V modules were regenerated and
validated; 91 unrelated arrays and binding symbols remain unchanged.

Full ordinary Windows x64/x86 builds passed at 0fcdbdd. The predecessor `003609f` local Windows runner passed all 64 suites. CI run 43065663481 passed all four jobs: full native Linux client and native Vulkan on Windows/macOS/Ubuntu. Windows passed 64/64 suites without skips; macOS/Ubuntu each passed 62/62 applicable suites plus two explicit Windows-only skips (sjk-chat and updater). Static analysis
completed all 55 direct contexts without parse errors; nine contexts retain
diagnostics and the historical 285-finding triage remains unchanged.
Actual final 0fcdbdd direct and FBO/HDR/MSAA cases passed all strict lifecycle checks (six map loads, 12 captures, video restarts, owned minimize/restore, exit 0, zero retained resources and no validation messages). Five additional configurations passed on the immutable scissor candidate; they are separate versioned evidence, not six cases on identical final binaries.
Validated 0fcdbdd is installed at `E:/Games/testClient/GameData`: launch `Play EJK Dev.cmd` for native Vulkan or `Play EJK Dev - OpenGL.cmd`. All 54 destination SHA256 checks passed, eight existing configuration hashes are unchanged, and overwritten files were backed up (`backup-20261111-171822`).

The initial `4dd25b0` GPU validation failed because the real branchless `MAX`
macro invalidated new image-dimension calculations; a safe macro substituted by
the initial CPU fixture hid the interaction. The production correction in
`0f92d64` uses `std::max`, tests actual stb macros and failed-preparation outputs,
and passed a corrected direct GPU case with three maps, video restart, odd PNG
captures, owned minimize/restore, normal exit and zero retained resources. The
initial failed runtime's external owned-process stop is not a clean-exit claim.
No additional loading/FPS improvement is claimed for these correctness fixes.
General Vulkan allocation/teardown recovery and every renderer state remain
unproven. A separate SDL/window bridge follow-up was blocked by automatic
approval review and remains unimplemented; it was not retried. See
[the audit inventory, evidence and remaining risks](../vulkan-audit/README.md).



### Resource and material follow-up at `81b10be`

Dedicated-memory, shader alignment, worker completion, selected Vulkan failure
handling and material-state fixes are recorded in [the versioned follow-up
ledger](../vulkan-audit/followup-81b10be.json). Focused coverage is 106 cases in
ten suites; earlier `0fcdbdd` measurements and validation remain historical.
Full ordinary Windows x64/x86 builds passed for the frozen 81b10be source; root verified explicit native-command exit 0 for both architectures. The complete local Windows runner passed all 66 discovered suites without skips. Compatible Clang 22.1.3 analysis completed 55/55 direct contexts with 13 diagnostic contexts retained and no parse errors.
CI run 43075400446 passed Windows (66/66), Ubuntu (64/64 applicable with two explicit Windows-only skips) and the full native Linux client. macOS failed only material_fog-invalid-t in vulkan-material-runtime (63/64 applicable suites passed); the exact assertion and production-versus-fixture cause are under investigation. The six ordinary 81b10be GPU profiles passed (23 map loads, 50 captures), as did weather and the corrected bot-shadow acceptance run (six maps, 33 captures). SSAA/HDR failed real driver validation for refraction blit destination width and dynamic-glow clear rectangles; owned polling aborted the process, with configuration/runtime preserved. Fullscreen/debug cases are unrun, corrections are in progress, and 81b10be will not be deployed.
TestClient still contains the validated 0fcdbdd build; 81b10be has not been installed. No new loading/FPS improvement is claimed;
global failure recovery and every renderer state remain unproven.

### SSAA and portability follow-up at `481800e`

The new [versioned follow-up ledger](../vulkan-audit/followup-481800e.json)
records SSAA shared-depth/target sizing, Vulkan offset portability and a portable
fog fixture with unchanged production assertions. Focused coverage is 121 cases
in eleven suites; failed `81b10be` evidence remains separately preserved.
Final ordinary Windows x64/x86 builds passed at 481800e; root verified explicit BUILD_EXIT=0 for both architectures. The full final local Windows runner passed all 67 discovered suites without skips. CI run 43078570667 passed all four jobs: full native Linux client and native Vulkan on Windows/macOS/Ubuntu. Windows passed 67/67 suites without skips; macOS/Ubuntu each passed 65/65 applicable suites plus two explicit Windows-only skips. The Mac material suite passed after the portable fixture correction.
On the actual 481800e runtime, all six basic profiles plus weather, bot/shadows and SSAA/HDR passed (32 accepted map requests, 92 captures). Fullscreen crashed with access violation 0xC0000005 during the first video restart, without a preceding validation error or forced termination; configuration/runtime hashes remained unchanged. Debug/mips remains unrun, so the complete 11-profile matrix has not passed. TestClient remains on validated 0fcdbdd; neither 81b10be nor 481800e is installed.
No new loading/FPS gain or exhaustive renderer-safety claim is made.

### Presentation and fatal-cleanup follow-up at `7bd3470`

[The separate 7bd3470 ledger](../vulkan-audit/followup-7bd3470.json) records
Vulkan presentation extent adoption and server-before-client fatal cleanup.
Coverage is 126 Vulkan cases plus five engine cases; old failures and the
renderer-only candidate/offline reassessment remain explicitly versioned.
Final ordinary Windows x64/x86 builds passed at 7bd3470 with explicit BUILD_EXIT=0 for both architectures. The final local Windows runner passed all 68 discovered suites without skips. CI run 43082756809 passed all four jobs: full native Linux client and native Vulkan on Windows/macOS/Ubuntu. Windows passed 68/68 suites without skips; macOS/Ubuntu each passed 66/66 applicable suites with two explicit Windows-only skips. Material and fatal-shutdown suites passed on Mac.
On identical 7bd3470 runtime binaries, 11 of 12 GPU profiles passed: six basic plus weather, bot/shadows, SSAA/HDR and both FBO/direct fullscreen cases (38 accepted map requests, 108 captures). The final debug/mip profile failed Khronos required-handle validation because vkUpdateDescriptorSets received dstSet VK_NULL_HANDLE; owned polling forced an abort and preserved configuration/runtime hashes. No complete matrix pass or deployment is claimed. TestClient remains on validated 0fcdbdd. The 81/481 stages were never installed, and 7bd3470 is not deployed.
No new loading/FPS or universal recovery/hardware-safety claim is made.

### Descriptor lifecycle follow-up at `750ff04`

[The versioned 750ff04 ledger](../vulkan-audit/followup-750ff04.json) records
the narrow descriptor-less image refresh guard, current-filter completion tests
and separately qualified debug candidate. There are 130 Vulkan plus five engine
cases; failed 7bd evidence and all earlier manifests remain intact.
Final ordinary Windows x64/x86 builds passed at 750ff04 with explicit BUILD_EXIT=0 for both architectures. The final local Windows runner passed all 68 discovered suites without skips. CI run 43087229764 passed all four jobs: full native Linux client and native Vulkan on Windows/macOS/Ubuntu. Windows passed 68/68 suites without skips; macOS/Ubuntu each passed 66/66 applicable suites with two explicit Windows-only skips, including expanded asset integrity.
Fresh 750ff04 GPU tests did not start: the launch guard found an existing user-owned TestClient and left it untouched. The complete 12-profile final-runtime validation is waiting for that client to close. The passed older-engine debug candidate remains separate evidence; no final 750 matrix pass is claimed. 750ff04 is staged only: 54 payload and 54 matching source SHA256 checks passed (108 checks), with eight protected configuration hashes unchanged at staging. Nothing was installed; TestClient remains on validated 0fcdbdd and older stages were never installed.
No new loading/FPS or universal renderer/hardware-safety claim is made.
