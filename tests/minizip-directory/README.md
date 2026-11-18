# Minizip directory compatibility and timing

This standalone fixture compiles the production `unzip.c` as C twice, with
identical compiler settings and bundled zlib: once with the frozen original
scalar readers from `reference_readers.h`, once from the working tree. The
reference replaces only the scalar-reader block; all other production parser
code stays identical. It does not stub parsing or decompression.
There are no external packages or game assets in this directory.

Configure and run from a Visual Studio developer shell:

```powershell
cmake -S tests/minizip-directory -B build/minizip -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/minizip
ctest --test-dir build/minizip --output-on-failure
python tests/minizip-directory/compare.py build/minizip/minizip_reference.exe build/minizip/minizip_candidate.exe build/minizip/fixtures 'E:/Games/testClient/GameData/Base/*.pk3' --extract-installed
```

Use the appropriate developer shell and a different build directory for x86.
The default reference needs no Git history and remains independent after the
optimization is committed, including in shallow checkouts. The frozen readers
retain their original copyright/license and identify their source revision.
Set `-DREFERENCE_REF=<commit>` for an explicit comparison with a complete
historical `unzip.c` instead.

The synthetic checks cover stored/deflated/empty entries, duplicate and Unicode
names, long names, comments, unknown extra fields, SFX prefixes, data descriptors,
ZIP64 central sizes and offsets, invalid CRCs, and every truncated prefix of a
small ZIP64 archive. Both implementations must return identical outcomes,
metadata, names, extras, directory positions, extraction sizes and CRCs. Valid
nonempty fixtures must extract successfully, and the bad CRC must fail. The
fixture records legacy rejection of a completely empty ZIP rather than changing
it. The private scalar-reader checks cover all 2/4/8-byte reads with 0 through 8
available bytes, callback caps of 1 through the field width, and EOF or error at
the end: 252 comparisons of return codes, values and final stream positions.

Non-error EOF is unusual here: `UNZ_EOF` and `UNZ_OK` both equal zero. The old
readers repeat the last byte across missing bytes (or zero if none were read).
This performance change preserves that behavior; it does not fix truncated ZIP
handling.

Installed packages are read in place. `--extract-installed` checks every entry
with both versions, including its uncompressed size and CRC, without writing
extracted files. The timing scan navigates and requests metadata once per entry,
matching the package loader's two central-header parses. It also hashes names,
metadata, comments, extras and offsets for equivalence. The script alternates
execution order, drops one warmup pair and reports seven-run medians plus all
samples. Read callback counts and bytes are reported separately. These are
warm-cache parser measurements, not game load times or gameplay FPS.
