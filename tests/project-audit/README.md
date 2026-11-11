This audit tracks first-party performance and correctness work on
`perf/loading-and-runtime`, starting from `6955dca`. The coverage ledger records
focused reviews separately from initial scans and outstanding candidates. A
completed source scan does not establish that every subsystem is optimized or
free of defects. Vendor code is excluded from the first-party count.

Run every applicable standalone regression suite from a compiler environment:

```
python tests/project-audit/regressions.py --output C:/build/ejk-regressions
```

Pass additional CMake options after `--` (for example `-G Ninja` or an explicit
NASM compiler). The runner discovers `tests/*/CMakeLists.txt`, retains separate
logs/results, and exits unsuccessfully if configuration, compilation or any
applicable CTest suite fails, including a suite with no registered tests. The
renderer CI runs this discovery on Windows/Linux/macOS.

`sjk-chat` and `updater` are explicitly Windows-only: their CMake files reject
`NOT WIN32`, and they test native Windows client implementations using WinHTTP
(plus BCrypt/Shell32 for chat). On other hosts the runner records each as
`status: platform-skipped`, `passed: null`, with the reason in `results.json`
and its log. Skips count separately from passes/failures; the pass denominator
contains every applicable suite. At checkpoint `8cc641a`, 30 suites were discovered:
Windows required 30/30 and Linux/macOS 28/28 with two explicit platform skips.
New suites join discovery automatically and increase those required totals.
No other configuration or test failure is converted into a skip.

For static analysis, generate `compile_commands.json` from the actual build.
CMake's compile database or Ninja's `-t compdb -x` for both C/C++ compiler rules
preserves each production target's defines and include paths:

```
python tests/project-audit/analyze.py --database BUILD/compile_commands.json --output ANALYSIS --clang-tidy /path/to/clang-tidy --jobs 4
```

The runner retains every distinct compiler context, including shared game code
compiled for multiple modules. It saves diagnostics, parse errors/timeouts,
source hashes and effective compile commands. `--path codemp/client/` limits a
follow-up run. It applies Clang's MSVC-header narrowing compatibility switch to
the two affected bot contexts and records that argument; production compiler
flags stay unchanged. Do not time game performance
while analysis or compilation is active.

The initial scan covered 560 contexts. Vendored `codemp/game/sqlite3.c` timed
out and was removed from the first-party denominator. Two bot contexts required
the recorded Clang narrowing compatibility switch. Rend2's Windows shader type
declaration needed a source fix. Including those focused retries, all 559
first-party contexts analyzed; 115 emitted diagnostics. Those are candidates,
not 115 confirmed bugs. See [safety triage](../project-safety/triage.md) for
verified issues and remaining examples. Later source changes receive targeted
analysis and regression checks; this count describes the initial audit pass.

The second complete run analyzed 560 first-party compiler contexts across 430
source files without parse failures or timeouts. It emitted 269 warning
appearances in 105 contexts. [The saved report](post-checkpoint-analysis.json)
retains source hashes and diagnostics per context. Source changed during the
broader work, so this records sampled files rather than attesting to one commit;
later runtime/prefix/ROFF changes need focused follow-up analysis. Fewer warnings
do not by themselves establish additional fixed bugs or safe caller contracts.

Loading measurements and their limits are documented in
[loading-audit](../loading-audit/README.md). Existing GPU-resident texture caching,
new parallel decoding and persistent prepared data are measured separately.
No head-to-head SJK result or general FPS improvement is implied.

`runtime.ps1 -Record` creates a rotating-camera `mp/ffa3` replay with seven bots.
Record once, then supply the same `-DemoFile` to both binaries, alternating their
order on a quiet machine. The runner saves the demo SHA-256, requires at least
1,000 completed timedemo frames, checks equal frame counts across repeats, and
uses identical 1080p settings. The current local recording has 5,005 frames.
Use `-LegacyCompletion` only when comparing an older client that aborts before
`nextdemo`; it provides a bounded command-buffer fallback after playback.
`-SeedVulkanCacheDirectory` starts each fresh profile from the same pipeline data.
Demo playback does not measure live server simulation or ordinary input
prediction, so collision-work reductions need their own production trace fixture.

`-RuntimeProfile 1` enables coarse aggregate Vulkan stages; `2` adds finer
Ghoul2/shader/visibility scopes. Use these diagnostic runs separately from FPS
comparisons: clock instrumentation changes timing and nested totals overlap.
Reset/report commands and the pre-shutdown report avoid per-frame logging.
Profiling defaults off and disabled scopes do not read the clock.

`cg_runtimeProfile` independently profiles cgame: `0` disables it, `1` records
coarse active-frame stages, and `2` adds aggregate entity/player stages such as
interpolation, model/equipment setup, bone angles, animation, shadows, sabers and
cosmetics. During gameplay, `cg_runtimeProfileReset` clears the counters and
`cg_runtimeProfileReport` prints them; cgame shutdown also reports before its
imports are released. `runtime.ps1 -CgameProfile 2` enables the fine mode for a
supplied replay. It defaults to `0`. There is no per-frame logging, and disabled
scopes read no clocks. Level `1` also skips all fine-stage clocks.

The separate idle-crowd recording on `mp/duel1` completes 5,005 replay frames.
Clientlist and screenshots verify 24 visible bots plus the observer; the coarse
profile has 5,003 completed active-frame samples, zero early returns, and
snapshot player counts of 24/24/24 (minimum/mean/maximum). Bot AI and movement
were frozen while ordinary client/Ghoul2 animation remained active. This is an
idle population workload, not combat, effects, live prediction or server
simulation. Its demo SHA-256 is
`5161FDE48C2BE772E9E44808C73B821F77C974631B0F5261BAFEE6A454879389`.

One quiet coarse diagnostic run, `crowd-idle-profile-quiet-1`, measured:

| Cgame stage | Milliseconds per completed active frame |
| --- | ---: |
| Whole active-frame call | 0.673392 |
| Packet entities | 0.507016 |
| Renderer scene submission | 0.114994 |
| Audio/refdef | 0.026028 |
| HUD | 0.015542 |
| Cvars/radial menu | 0.001786 |
| Prediction | 0.000313 |

Packet entities account for about 75% of that sampled cgame call, motivating
the finer player stages. These are inclusive owning-thread elapsed times:
nested stages overlap and must not be summed. Renderer counters use their own
frame denominator; the same run's backend was 0.848853 ms/frame, including
0.496682 ms/frame in queue present. Fine clock instrumentation adds overhead,
so profile runs diagnose costs and are kept separate from uninstrumented FPS
comparisons. These data establish neither a speed improvement nor a general
gameplay bottleneck.

Three alternating pairs of clean MSVC x64 Release builds at `614e46e` measured
LTO independently. Normalized production compiler databases matched after
removing `/GL`; only LTO links had `/LTCG`. Both used byte-identical assets and
dependencies, fresh profiles, the same recorded demo and seeded pipeline cache.
No compiler, analyzer or other local game client ran during timed playback.

| 5,005-frame replay | Round 1 | Round 2 | Round 3 | Median |
| --- | ---: | ---: | ---: | ---: |
| Normal Release FPS | 734.6 | 744.6 | 755.0 | 744.6 |
| LTO Release FPS | 775.8 | 778.0 | 779.8 | 778.0 |

The median improvement is about 4.5% for this workload. It is separate from the
loading results and does not establish a live-server or general FPS improvement.
`UseLTO` now defaults on for Release when C and C++ support checks pass. Configure
with `-DUseLTO=OFF` to disable that automatic default. Explicit global/per-config
CMake IPO settings retain precedence. Unsupported toolchains report why they
use ordinary compilation; Debug flags and unrelated build errors stay intact.

The idle-crowd profile then separated shadow impact marks from their world trace
and water tests. In one fine diagnostic, impact marks used 0.171732 ms per
completed active frame, versus 0.016685 for the shadow trace and 0.007889 for
water contents. Removing two fully overwritten temporary-array initializers
from Vulkan/vanilla polygon clipping preserves the resulting polygon vertices;
the production-function fixture compares 12,000 randomized convex polygons,
boundaries and six-plane sequences on x64 ASan and x86 Release.

Five alternating uninstrumented pairs after one warmup pair measured that Vulkan
change independently. The engine, cgame, game/UI, runtime DLLs, asset PK3s and
pipeline seeds were byte-identical; only the active renderer DLL differed.
Both used the same 5,005-frame idle-crowd demo and fresh 1080p profiles. No
compiler, analyzer or other local game ran during playback.

| Idle 24-bot replay FPS | Round 1 | Round 2 | Round 3 | Round 4 | Round 5 | Median |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Original clipping | 603.2 | 644.3 | 639.9 | 638.9 | 641.3 | 639.9 |
| Redundant clears removed | 618.4 | 649.2 | 653.7 | 654.9 | 653.1 | 653.1 |

The median was about 2.1% higher and every paired trial improved. Both variants
also drifted over the sequence, so this remains a result for this idle scene,
not combat, live prediction, general FPS or SJK performance. Initialization
counts alone do not prove emitted machine stores. Local results and module
hashes are retained in `project-clipping-runtime-benchmark/results.json` and
`clipping-runtime-manifest.json`; this is separate from the LTO experiment.

Four further diagnostic scopes distinguish animation sound triggers, pose
playback, bone-frame queries and event scans/dispatch. In
`crowd-idle-animation-fine-profile-1`, sound triggers used 0.125934 ms per
completed active frame, including 0.115993 in event scans/dispatch; playback
used 0.002334 and bone queries 0.006662. Those nested instrumented values
identify the event lookup as the next target and do not establish a speed gain.

Clean event-index builds then measured that change independently. All 58
normalized cgame/cJSON compiler contexts and embedded metadata matched; only
the cgame DLL differed in otherwise identical runtime folders. Five alternating
idle-crowd pairs after one warmup pair gave control FPS
611.0/651.9/650.4/652.2/646.7 and indexed FPS
632.2/652.9/654.4/658.2/653.7: medians 650.4 and 653.7, about 0.5% apart.
Both variants drifted during the sequence. The fixture's 94.5% reduction in
visited event slots is not an equivalent reduction in total frame time.
Actual branch counters confirmed that all 120,462 sampled queries used the
index; nested event dispatch accounted for most of the remaining cost.

In this replay every sampled dispatch was a footstep. Fine footstep stages
measured 0.003846 ms per completed active frame in bolt work, 0.001965 in water
traces, 0.002022 in ground traces and 0.097743 in sound submission, with no
footstep effect or footprint-mark calls. This is the software audio workload
(`s_useOpenAL 0`) used by the replay harness; it is not an assertion about every
audio backend or scene.

`runtime.ps1 -SoundProfile 1` enables default-off backend submission timers.
`s_runtimeProfileReset` and `s_runtimeProfileReport` also work in the console;
sound shutdown reports before cleanup. The diagnostic clock is `steady_clock`
and never uses the event-pumping game clock. In
`crowd-idle-sound-backend-profile-1`, 21,144 cache touches used 452.053 ms,
versus 33.810 ms in the rate limiter, 4.147 in channel selection and 1.530 in
channel setup. The old cache timestamp reaches Windows console-input polling
through `Com_Milliseconds`. The implemented cache-clock change uses the direct
system clock only when journaling is initialized and off. Recording, playback
and uninitialized state retain the legacy call sequence. Actual journal bytes,
queue delivery and LRU lifetime fixtures pass; controlled game timing remains
separate from these diagnostic and avoided-work results.

The single-pass archive catalogue experiment is recorded in
[loading-audit](../loading-audit/README.md): five same-binary alternating pairs
reduced median initial filesystem startup from 318 to 222 ms and the complete
local three-map cycle from 8.0068440 to 7.5329079 s. That is a separate loading
experiment, with unchanged quality and exact catalogue/purity/reference checks.

Clean checkpoint `417b060` passed full Windows x64/x86 Release builds and all
51 then-discovered standalone suites, plus visible-world/menu/restart checks
with Khronos core/synchronization validation and packaged-module execution.
The same folder's user configuration was preserved during refresh. A separate
11-context production follow-up analyzed without parse failures/timeouts and
retained 36 warning appearances for review. See the `followup_417` section of
[the integration report](integration-analysis.json) for exact sampled hashes and
contexts. Later sound-clock, EjectAll, AI and parser changes require their own
checks; these results do not mark the project-wide audit complete.

The sound-clock comparison used clean `6d9c5f4` and `68858c44` engine builds
with identical pinned version metadata, compiler/link settings, imports and
exports. The only production source difference was `SND_TouchSFX`; only the
engine executable differed between runtime folders, with 13 identical common
files. Five alternating, profiling-off idle-crowd pairs followed two warmup
pairs, using the same 5,005 frames, fresh profiles and seeded pipeline cache.
No compiler, analyzer or other game ran during timing.

| Sound-cache clock FPS | Round 1 | Round 2 | Round 3 | Round 4 | Round 5 | Median |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Original event clock | 651.9 | 651.2 | 653.7 | 650.3 | 652.5 | 651.9 |
| Direct clock when journals are off | 699.2 | 702.9 | 701.8 | 698.7 | 699.2 | 699.2 |

That is about 7.3% higher median FPS in this software-audio idle replay; every
pair improved. Audio settings, samples and cache semantics were preserved.
Fixed-frame timedemos advance game time faster than wall time, so this does not
establish a general live-combat, input-prediction, server or OpenAL result.
Post-change diagnostic cache touches fell from 452.053 to 1.054 ms, while the
footstep sound group fell from 504.070 to 19.979 ms. Registration, entity loops,
ambient loops and successful loads also touch the cache outside the ordinary
playback-only diagnostic; their extra call counts are not measured here.
Result rows and immutable module hashes are retained in
`project-sound-clock-runtime-benchmark/results.json` and
`sound-clock-client-manifest.json`. This is separate from the LTO, clipping,
index and loading experiments; percentages must not be added together.

`server.ps1` runs a separate normal local workload with a loopback-only dedicated
server, 24 active bots and one fresh spectator. It checks process-owned network
bindings, stock bot population at both boundaries, combat evidence, profile
reports, screenshots and normal exit. It leaves AI/movement enabled and does
not change their cadence. A failed first capture was retained separately; the
successful `server-active24-profile-2` run adds a frame wait before final quit.

At `4ba0d0c`, the 20.014-second sample verified all 24 bots and 25 in-use actors
at both boundaries, with 15 bot-vs-bot kills. Mean elapsed values were
1.871372 ms per eligible server call, 0.783194 per bot-AI call, 0.926051 per game
tick and 0.159595 per client-message call. The first, AI and message counts were
800; game tick count was 801 because of catch-up. Nested totals overlap. These
are diagnostics of the updated build, without a before/after server baseline;
one observer also does not represent 24 human clients' network cost.

The committed `4ba0d0c` tree passed ordinary full x64/x86 builds, native Vulkan
world/menu/restart/resource checks and packaged-module execution. CI run
42978948224 passed all 55 committed Windows suites and all 53 applicable
Linux/macOS suites with two explicit Windows-only skips, plus the complete
Linux native-Vulkan client build. The earlier Ubuntu fixture failure at
`417b060` was an omitted standard integer header, fixed explicitly in `28adafe`.

The full audit remains unfinished. Automated content review blocked the
remaining bot-parser and loop-sound regression tasks and a delegated validation
task. The unfinished bot-parser diff and loop fixture are preserved outside the
repository, with owned source restored and untracked test work excluded from
committed test discovery. They are absent from the playable build and are not
reported as fixed. Earlier extra Icarus consumer cases also remain unverified;
only the separately tested Get/GetVector/Move paths are recorded as fixed.
