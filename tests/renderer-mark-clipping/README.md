This fixture compiles the actual `R_ChopPolyBehindPlane` functions from Vulkan
and vanilla, together with the actual scalar `DotProduct`/`VectorCopy` helpers.
`baseline-chop.h` freezes the original Vulkan function from `e495b4c`.

Both production functions must produce byte-identical complete output buffers
against that reference, including untouched storage and canaries. Coverage
includes 12,000 deterministic random convex polygons per renderer, positive/
negative/on-plane vertices at epsilon boundaries, zero/negative/maximum input
counts, and repeated six-plane clipping of newly generated intersections.
The new empty-input guard returns before reading the closing sentinel; the
existing upper bound still rejects polygons that could overflow the output.
ASan checks the same production code, rather than a reimplemented clipper.

A second generated copy instruments only the original array initializers.
It verifies that the reference initializes both 68-element arrays (544 bytes
on the supported targets) and the changed functions perform neither redundant
initialization. Every active entry and closing sentinel remains assigned by
the original classification loop. This counts removed source initialization
work; it does not measure emitted memory stores or establish an FPS benefit.

To reproduce the failing-before work assertion, provide an older `codemp` source
tree with `-DMARKS_SOURCE_DIR=...`. Both old renderer functions still pass the
output comparisons and fail the zero-redundant-initialization assertion.
The current change passed x64 ASan and x86 Release. Whole-game FPS impact has
not yet been measured; that requires a quiet matched binary comparison.
