Compiles the production Vulkan shader catalogue loader, cache, lookup, compressor
and engine tokenizer with filesystem/cvar/hunk services supplied by the fixture.
Checks exact compressed bytes, bucket order, duplicate precedence, references and
fresh hunk relocation across unchanged loads, changed source identities, reordered
or changed listings, loose edits, unavailable providers, journal modes, disabled
services, shutdown and the retention bound. Unchanged cache hits perform no file
reads or token parsing. This fixture does not measure complete game loading.

Configure with CMake, build, then run CTest. `r_shaderTextCache 0/1` selects the
ordinary/cache path for real-client alternating comparisons; it defaults to 1.
The local combined filesystem/audio/catalogue comparison reused 762 shader files
(3.28 MiB) in about 18–24 ms. Combined map-loading gains do not isolate this cache
or establish a speed advantage over SJK; see the loading-audit measurements.
