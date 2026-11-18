These standalone tests compile the production navigation serializer, loader,
node lifetime methods and node declaration. File operations use a bounded
in-memory service stub; the fixture counts actual node allocations/deletions.

Configure with CMake, build, then run CTest. AddressSanitizer is recommended.

- `nav_roundtrip`: production-save/production-load byte-identical round trips
  for empty graphs and graphs of 1, 3 and 128 nodes. Also verifies table reads
  scale per node rather than per rank.
- `nav_partial_cleanup`: reject a bad second node after the first was inserted,
  closing the file and freeing both the failed node and partial graph.
- `nav_truncations`: every truncation of a valid graph, plus short reads even
  when the opened file advertises the original complete size.
- `nav_invalid_tables`: invalid headers/checksums, negative/oversized node,
  edge and rank counts, invalid node/edge/failed-edge indices, and failure after
  a previous successful load.

To reproduce the previous cleanup defect, configure a separate build with
`-DREFERENCE_REF=6955dca` and run `ctest -R nav_partial_cleanup`. That case fails
against the old production implementation. Avoid running hostile-count cases
against the old unbounded allocator.

The executable also accepts `metrics` and `file <navigation-file>` for local
read-count and byte-identical compatibility checks. A 128-node graph required
17,668 filesystem reads at `6955dca`, versus 1,156 after bulk table reads. This
is a call-count measurement, not a wall-clock loading-time claim.

Local validation (2026-11-11): MSVC x64 AddressSanitizer passed all four tests.
All 25 installed navigation files found in the test GameData PK3 archives
(24 stock files and one Gauntlet file) loaded and saved byte-identically. The
files are game assets and are not included in the repository.
