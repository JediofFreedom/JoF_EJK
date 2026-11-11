This suite builds the actual bundled-backend chooser, modified minizip and
libpng, and extracts the production PNG decoder without rewriting its body.
It checks streaming/reset/dictionary behavior, independently encoded raw/zlib/
gzip streams, corruption and truncation errors, allocator ownership, ZIP CRC
failure and offset reopening, and exact RGBA bytes for ordinary, 16-bit,
paletted/transparent, low-bit grayscale and Adam7 PNGs. PNG truncation, bad CRC
and allocator failure must return failure with no leaked output.

Configure normally to test the default zlib-ng backend, or pass
`-DUseZlibNG=OFF` to run the same checks against classic zlib. The configure
assertions deliberately enable parent Release/custom-config IPO, native-host
instructions and shared libraries: zlib-ng must remain static, with every IPO
configuration OFF, while those parent settings remain unchanged. A separate
policy fixture and actual chooser configurations check the explicit CMake
3.10-3.13 and unsupported-compiler fallback decisions. The legacy CMake fixture
simulates the version input; it does not run an old CMake executable. A license
install fixture verifies the preserved notices and rejects unexpected installed
headers or libraries. Both backend variants passed the real codec tests under
AddressSanitizer on x64 and x86.

`UseZlibNG` defaults to `ON` and takes effect with `UseInternalZlib=ON`, CMake
3.14 or newer and a supported compiler. Older supported EJK CMake versions and
unsupported compilers retain classic zlib with a configure diagnostic. macOS
universal builds with multiple distinct `CMAKE_OSX_ARCHITECTURES` also use
classic zlib: upstream selects optimized source files from only the first
architecture. Single-architecture builds, including repeated identical entries,
remain eligible for zlib-ng; the chooser policy fixture covers both cases.
`UseZlibNG=OFF` selects classic bundled zlib 1.3.1; `UseInternalZlib=OFF` retains
the external zlib package choice. Both production internal backends use the same
modified bundled minizip and bundled PNG implementation.

The pinned zlib-ng 2.3.3 library uses the zlib-compatible API, runtime CPU
detection and generic fallbacks, with host-native ISA selection disabled. Its
IPO/LTO is OFF for every configuration while parent Release LTO remains intact.
It links statically, without a new runtime DLL. The preserved license and
upstream record install under the game's `licenses/zlib-ng` directory; see
[EJK-UPSTREAM.md](../../lib/zlib-ng/EJK-UPSTREAM.md) for the exact revision and
license.

Measured performance is workload-dependent. Three alternating warmed library
pairs reduced ZIP extraction of 824,071,333 bytes / 8,377 members from a median
1,651.93 to 1,309.80 ms, and production decoding of 128 installed PNGs from
455.180 to 360.055 ms: about 21% less time in those isolated workloads. A full
installed-archive compatibility pass matched CRCs and aggregate SHA-256 over
14,501,374,289 expanded bytes, and the PNG corpus matched decoded RGBA bytes.

That is not a 21% whole-game loading improvement. Five alternating whole-client
pairs using matched `383785a` builds produced effectively flat local three-map
cycles (8.228 versus 8.225 s). Three alternating pairs on the heavier real-server
`yavin_temple_mp2` workload reduced median cgame initialization from 2,465 to
2,276 ms (7.7%) and connect-to-first-rendered-frame from 5,408 to 5,202 ms (3.8%).
All three paired cgame loads improved; gamestate arrival remained effectively
unchanged. Joins include variable network/server work, and these comparisons do
not establish an FPS gain or a lead over SJK.

See the [loading audit measurements](../loading-audit/README.md#portable-zlib-ng-backend)
for settings, scope and result-file names. This correctness suite does not run
timed benchmarks or assert timing thresholds.
