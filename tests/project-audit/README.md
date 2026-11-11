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
contains every applicable suite. With the current 30 discovered suites, Windows
requires 30/30 and Linux/macOS require 28/28 with two explicit platform skips.
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

Loading measurements and their limits are documented in
[loading-audit](../loading-audit/README.md). Existing GPU-resident texture caching,
new parallel decoding and persistent prepared data are measured separately.
No head-to-head SJK result or general FPS improvement is implied.
