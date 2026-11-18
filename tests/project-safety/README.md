The standalone tests compile the actual production functions with engine-service
stubs. `filter_bounds` covers short names, malformed character classes, large
wildcard literals, case matching and the existing prefix-match behavior.
`recording_options` covers disabled/negative recording indicators, all supported
display modes, and a zero-duration frame while the FPS indicator is enabled.
`server_entity_bounds` checks normal/legacy entity mappings and verifies invalid
or null game entities drop safely before the server array is accessed.

Configure with CMake, build, then run CTest. Use AddressSanitizer to check the
exact-size filter/name buffers. `-DREFERENCE_REF=6955dca` reproduces the original
failures using the same fixtures.

The additional executable compiles the actual console padding expression,
pause-expiry client loop, complete BSP cheat command, and dedicated shader
vector parser/constant-color branch. The cases cover absent/nonpositive console
scale and fractional rounding, eligible pause-message recipients, initialized
spawn coordinates, and rejected incomplete color vectors.

`snapshot_option` compiles `CG_DrawSnapshot` and covers disabled/negative settings,
both positive display modes, all six font branches, and Asian/sharp-HUD fallback.
Nonpositive settings must return without touching a missing snapshot.

MSVC x64 AddressSanitizer passes all eight cases. Against `6955dca`, the original
three cases fail; the four additional cases fail in a Debug build, and the
snapshot case fails through the MSVC undefined-read runtime check. The Debug
fixtures install a runtime-check handler so a failure exits instead of opening
an interactive dialog. Use a separate baseline build without AddressSanitizer
when enabling MSVC `/RTC` checks.
