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
