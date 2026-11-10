# Loading performance checks

This harness compiles the production shader lookup, PK3 loader, filesystem
listing, string comparisons, token parser, MD4 checksums, minizip and zlib.
Allocation, directory enumeration and other engine services are stubbed.
Python 3 creates compressed ZIP fixtures; no game assets are distributed.

Checks cover shader priority and case matching, hash collisions, empty buckets,
lookup after hash-table invalidation, per-call file-list state, duplicate names,
pure-package filtering, loose files, the list limit, empty paths, invalid ZIPs,
entry order, offsets, lengths, CRC reads, regular/pure checksums and cleanup.
The optimized shader test also bounds parsed tokens so a full-library scan
cannot quietly return. Timing has no pass/fail threshold.

Build and run from the repository root:

```powershell
cmake -S tests/loading-performance -B build/loading-check -A x64
cmake --build build/loading-check --config Release
ctest --test-dir build/loading-check -C Release --output-on-failure
```

Build the same harness against a previous commit to compare production code:

```powershell
cmake -S tests/loading-performance -B build/loading-reference -A x64 -DREFERENCE_REF=e3aba1f966179947f1b252c28b1a2d8e4c0d8944
cmake --build build/loading-reference --config Release
python tests/loading-performance/benchmark.py build/loading-reference/Release/loading_performance.exe build/loading-check/Release/loading_performance.exe build/loading-check/fixture.pk3
```

Append installed PK3 paths to the Python command to validate and measure their
indexes. Both versions must return identical checksums. The script alternates
their order, discards a warmup pair, then reports five-run medians. Shader
lookups use 12,002 synthetic definitions and 400 implicit-image misses. Listing
uses 6,000 unique names in two package search paths, repeated eight times.

For whole-client measurements, retain separate Release binary directories for
the old/new revisions, each built with `-DBuildPortableVersion=OFF`. The candidate
directory must contain the built `EternalJK/*.pk3` assets, and the installation
must supply `SDL2.x86_64.dll`. Run:

```powershell
./tests/loading-performance/smoke.ps1 -ReferenceDirectory C:/bench/reference -CandidateDirectory C:/bench/candidate -GameData C:/Games/JediAcademy/GameData -OutputDirectory C:/bench/results
```

Repeat with `-Renderer rd-rend2-etjk` for Rend2. It launches a 640x480 window,
loads `mp/ffa3` with sound enabled and asynchronous map loading disabled, waits
300 frames, checks renderer selection and map completion, then exits. Each run
uses an isolated home directory and skips player config files. JSON records
both process duration and the engine's `CL_InitCGame` timer. The process duration
includes renderer startup, the frame wait and shutdown; it is not a pure map
load timer. GPU shader caches and OS file caches can affect timings, so compare
repeated runs. These checks do not measure gameplay FPS or establish a speed
comparison with SJK.

## Measurements from this change

Compared with beta commit `e3aba1f966179947f1b252c28b1a2d8e4c0d8944` on
Windows, MSVC 19.44 Release x64, Ryzen 7 5800X3D and RTX 5070 Ti. The game
installation had 256 base PK3s (about 9 GB). All 256 packages passed index,
offset, length, checksum and decompression checks.

| Measurement | Reference | Candidate |
| --- | ---: | ---: |
| Whole vanilla client cycle, median of 3 runs | 21.107 s | 12.306 s |
| Vanilla `CL_InitCGame`, median of 3 runs | 4.20 s | 2.11 s |
| Rend2 `CL_InitCGame`, repeated warm run | 4.04 s | 2.65 s |
| 400 vanilla shader misses, median of 5 runs | 226.125 ms | 0.078 ms |
| 8 large file listings, median of 5 runs | 351.889 ms | 9.624 ms |
| Fixture + stock assets0/1/3 PK3 indexing, median of 5 runs | 171.035 ms | 89.758 ms |

The vanilla client cycle was 42% shorter and its map initialization about 50%
shorter. The shader test parsed 1,795 tokens instead of 33,605,395. Those are
measurements of these workloads on this machine, not universal speedup claims.
The ZIP loader temporarily retains metadata for one package while building its
final index; it releases that extra storage on return. Malformed directories
and entry names that cannot fit `MAX_ZPATH` reject the package and close its
handle, instead of constructing a partial index.
