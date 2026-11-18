This suite compares `fs_zipCatalogFast 0/1` through actual production archive
loading, mount order, provider lookup, checksum and minizip code. The new mode
uses engine-only first/next-with-info functions, sharing the existing ZIP parser.
It does not cache catalogues persistently or alter generic minizip APIs.
The production cvar defaults to `1` after the controlled comparison below.
Explicit `0` selects a fresh legacy navigation/GetInfo scan.

Valid catalogue iteration must preserve every public metadata field, filename,
legacy-width size conversion, saved offset, private current-file state and
stream cursor. Checks cover 303 existing ZIP fixtures plus six catalogue cases,
ZIP64 metadata above 4 GiB, SFX prefixes, duplicates, comments, unknown extra
fields, truncated archives, partial read callbacks, EOF/error callbacks,
zero/short filename buffers, null metadata output, seek failures, saved-position
navigation and payload extraction/CRC after the new iterator.

The new combined API deliberately invalidates its current file on any parse
error, including a filename read failure. Legacy navigation followed by failing
GetInfo can leave the old current-file flag true. Both FS modes immediately
reject/close the failed package; neither retries from a failed iterator. The
new API documents this difference, while the generic APIs remain untouched.
Known bad central headers, overlong names and truncated filenames are rejected
by both FS paths. Other malformed-input outcomes retain the legacy parser's
behavior, including its unusual `UNZ_EOF == UNZ_OK` scalar-reader convention.

The FS comparisons use complete serialized records, rather than relying solely
on hashes: entry order/names/sizes/offsets, ordered CRC checksum words, regular
and pure checksums, duplicate hash-chain order, sorted name indexes, pure and
nonpure provider winners, and reference flags must all match. Every installed
member also receives a field-by-field legacy/new metadata and CRC comparison.
The existing `filesystem-restart-cache` suite runs its restart, replacement,
purity and reference scenarios with both catalogue modes.

Build normally and run CTest. Optionally append an installed package directory
after the generated fixture directory; installed files are read in place:

```
filesystem_catalog_iterator /build/catalog/fixtures /Games/JediAcademy/GameData/Base
```

On the installed 256-package / 75,658-member corpus, x64 ASan and x86 Release
produced identical complete FS records (summary fingerprint
`ad2cc24b68a6a1d6`). Counted catalogue work was:

| Work | Legacy | Fused |
| --- | ---: | ---: |
| Central-header parses | 151,572 | 75,914 |
| Read callbacks | 2,653,276 | 1,375,934 |
| Seek callbacks | 269,197 | 126,876 |
| Bytes read | 26,913,347 | 23,299,451 |

The parse-count assertion requires `2N+A` to become `N+A` for `N` entries in `A`
nonempty archives: one existing open-time parse remains per archive. Configure
`-DCATALOG_REFERENCE_ITERATOR=ON` to compose the original navigation/GetInfo
operations behind the new interface. All output/state checks pass, then the
eliminated-parse assertion fails, demonstrating the old duplicate work.

Printed fixture mount timings are instrumented diagnostic samples; compilation
and other activity may be present. The following separate whole-client runs
establish the measured startup effect. They used one Release/LTO binary with
only `fs_zipCatalogFast 0/1` changed, identical assets/quality/pipeline seeds,
fresh profiles, warm OS caches, 2048 MiB texture residency, `fs_restartCache 1`
and `r_textureSignaturePrefix 1`. No compiler, analyzer or other game client ran
during measurement. One priming pair was excluded, followed by five alternating
pairs. Samples below follow the actual measured round order.

| Round | Initial FS, legacy ms | Initial FS, fused ms | Complete cycle, legacy s | Complete cycle, fused s |
| --- | ---: | ---: | ---: | ---: |
| 1 | 315 | 219 | 8.1650352 | 7.7436499 |
| 2 | 319 | 227 | 8.0068440 | 7.5277146 |
| 3 | 318 | 221 | 7.9175396 | 7.5329079 |
| 4 | 320 | 225 | 7.9187953 | 7.5510236 |
| 5 | 318 | 222 | 8.1457708 | 7.5266485 |
| Median | 318 | 222 | 8.0068440 | 7.5329079 |

Initial filesystem startup was about 30.2% shorter; the complete T1_Fatal /
mp/ffa3 / T1_Fatal client cycle was about 5.92% shorter. Both improved in every
pair. The complete cycle includes startup, fixed waits and shutdown; this is
not 30% faster whole-game loading, an FPS result or a percentage to add to prior
gains. Raw results are retained locally as
`project-catalog-runtime-benchmark-v2/results.json`; the earlier run was excluded
for a harness-label mismatch, not a game failure. Use
`tests/loading-audit/profile.ps1 -ZipCatalogFast 0/1` for fresh comparisons.
