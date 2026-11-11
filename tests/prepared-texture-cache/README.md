The standalone suite compiles the production prepared-texture cache and extracts
the production writable-path helper. It checks format/dimension/mip validation,
every header-byte corruption, payload corruption, invalidation by source and
settings, persistent reads across cache instances, disk and queue budgets,
exclusive writer ownership, stale temporary-file cleanup, and concurrent reads
during atomic replacements. Both x86 Release and x64 AddressSanitizer builds
have been exercised on Windows.

Build with CMake and run CTest. No game data, Vulkan driver, or SDK is required:

```
cmake -S tests/prepared-texture-cache -B build/prepared-texture-cache
cmake --build build/prepared-texture-cache
ctest --test-dir build/prepared-texture-cache --output-on-failure
```

The renderer setting `r_textureDiskCacheMegs` is experimental and defaults to 0
(disabled). A value such as 2048 enables a bounded disk cache under the actual
writable game directory at `cache/vulkan/textures-v1`. x64 permits up to 4096 MiB;
x86 permits up to 1024 MiB. Its asynchronous writer queue is separately limited
to 64 MiB on x64 and 16 MiB on x86. When that queue fills, writes are skipped.
Eviction uses file modification age; ties can be evicted in either order.

Only JPG/PNG textures eligible for deferred frontend registration use this cache.
The main thread reads the original source through the engine filesystem before
dispatch, retaining its search priority and pure-server package accounting.
Workers hash the actual encoded bytes and processing settings with stable
XXH3-128, then validate the saved metadata and mip payload. An invalid or missing
entry follows the ordinary decode/prepare path. Prepared 16-bit texture entries
retain the RGBA8 CPU mip chain; existing GPU-format conversion still runs once.
Source changes, texture settings, color tables, architecture, and preparation
version change the cache key. Increment the key/format version in
`vk_prepared_cache.cpp` whenever preparation semantics change.

Each registration job captures its own processing context, including world/UI
phase, greyscale, picmip, format, mip filtering and color tables. Model and UI
textures retain the context at their request even if registration later enters
world loading or live cvars change. Drains precede backend execution, world
phase changes, and sampler/gamma updates. Startup splash/procedural textures,
transfer sources and backend-discovered images remain immediate. Cancellation
joins workers before image or source lifetimes end.

`r_textureWorkerPrepare 1` also prepares greyscale, resized pixels, mip chains
and compression on the decoding workers; `0` retains main-thread preparation
for a controlled comparison. Both paths use the same production algorithms and
captured context, so this switch is not part of the disk-cache key. The upload
vector moves directly into the pending result. GPU allocation, format conversion
and upload recording remain on the main thread. stb_dxt's lookup tables are
initialized on the main thread before any compression workers start.

Preparation jobs reserve encoded bytes plus 20 bytes per source pixel and
64 KiB of overhead within the existing 128 MiB/x64 or 32 MiB/x86 pending budget.
That includes decode pixels, the worst rectangular mip chain and filtering/
compression scratch. Each job's private scratch is separately capped at 16
bytes per pixel plus 64 KiB, with exact-sized allocations. Oversized jobs drain
the pending queue before using the synchronous path; scratch allocation failure preserves pixels and falls
back to main-thread preparation without applying greyscale twice. The main
thread's initial encoded-file read/probe is a transient allocation before job
admission. Profiling reports completed worker preparations, main-thread
fallbacks, oversized loads and summed preparation effort separately.

The loading-audit suite compares 240 production preparation fixtures on four
concurrent threads, including tiny/rectangular textures, HQ and simple mip
filtering, picmip, fractional/full greyscale, colored mips, BC3 and both 16-bit
formats. It checks exact bytes against the main path, rejects engine scratch/
Hunk calls on those workers, and exercises exhausted private scratch budgets.

The writer uses an exclusive native file lock across clients and publishes
complete entries by atomic rename. Other clients can read the cache while their
own writers remain disabled. Interrupted temporary files are reclaimed on the
next writable cache startup. The cache is best-effort: write, allocation, or
directory failures do not prevent normal texture loading. Checksums detect
accidental corruption; they are not cryptographic authentication.

Keep cache creation separate from warm-cache timing, include the original file
read and checksum costs, and stop concurrent builds/static analysis during
measurements. Fresh profile directories require copying the prepared cache from
the creation run to measure a warm cache across processes. Vulkan core and
synchronization validation plus screenshots should verify both cache hits and
ordinary fallback, including 16-bit uploads and renderer restarts.
