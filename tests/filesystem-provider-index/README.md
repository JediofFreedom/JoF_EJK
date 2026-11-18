This suite compiles the production file resolver, filename hash/comparison,
read-identity wrapper, pure-package reorder function and new provider index.
The small engine-service harness is shared with the loading audit. Tests run
both `fs_lookupIndex 0` (linear) and `1` (indexed).

Coverage includes mixed directory/archive priority, fresh loose-file additions,
edits, deletion and zero-length files; live pure membership and reference flags;
configuration/private-file restrictions; case/separator matching and legacy
colon/hash eligibility; actual pure-list reordering; provider replacement;
snapshot lifetime; and duplicate ZIP members through both shared and independent
real minizip streams. CMake also verifies mount/shutdown invalidation hooks and
that shutdown drops the index before freeing packages.

Configure with CMake, build and run CTest. MSVC x64 AddressSanitizer and x86
Release are supported. Running the executable with its build directory as its
sole argument prints synthetic lookup metrics in addition to the checks.

The synthetic sample contains 256 archives of 256 files, performs 4,000 hits in
the lowest-priority archive and 4,000 misses, and includes the cold index build.
It reports legacy filename comparisons, pure checks, index memory and build
time. Those timings isolate provider lookup; they are not full-game loading
measurements. Real game A/B runs must include initial index construction and
use identical packages, maps, cache budgets and worker settings.

One local x64 Release sample: 25.38 ms linear versus 6.93 ms indexed, including
the 4.16 ms cold build. The index occupied 2 MiB; legacy filename comparisons
fell from 508,688 to 6,642, and pure checks from 266,429 to 4,000. The x86 index
occupied 1.5 MiB. These are synthetic sample results, not a promised game-load
speedup.

The index keeps immutable package-member candidates, not cached winners or
negative results. Every eligible loose directory is checked in original order,
and the original resolver still checks the package bucket, pure rules and
reference flags. The snapshot is rebuilt after provider-list changes. Its
accelerator allocation is capped at 64 MiB on x64 or 32 MiB on x86; unavailable
or oversized indexes fall back to linear lookup until the next invalidation.
`cl_loadTimings 1` prints member count, allocated index bytes and construction
time on each successful rebuild.

`fs_metadataProbe 1` additionally uses a live Windows attribute query to skip CRT
stream creation for definitely missing loose metadata candidates. It defaults to
1; use 0 for the ordinary metadata path. Existing files, uncertain errors, non-ASCII
paths and DOS device aliases keep the ordinary open/length/close path; other
platforms keep ordinary opens. This does not retain directory contents or negative
results. The suite runs both probe settings and checks avoided stream opens,
fresh overrides, edits/deletions, empty files and Windows sharing failures.

A combined full-game comparison enabled this probe, shader-text reuse and fast
equivalent PCM conversion together in one binary, with five alternating pairs.
Median map handoffs for T1_Fatal / mp/ffa3 / T1_Fatal revisit changed from
1,891 / 823 / 555 ms with all three disabled to 1,725 / 725 / 464 ms enabled.
Texture source metadata/signature medians changed from 97.008 / 99.573 / 107.203 ms
to 62.729 / 63.853 / 68.764 ms. These are combined results for the three changes;
the whole map-loading gain cannot be attributed to this probe alone.
