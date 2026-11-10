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
