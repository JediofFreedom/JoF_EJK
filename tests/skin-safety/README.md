Configure this directory as a standalone CMake project and run CTest. Tests
compile the production skin parser/registration functions for vanilla, Vulkan,
Rend2, dedicated, and Rend2's legacy JK2 conditional path.

Fixtures cover short filenames and surface names, `_off`/`*off` handling,
case-preserving shader lookup, case-insensitive skin caching, truncated quoted
tokens, comments, three-part skin macros, and derived paths at the buffer limit.
Use AddressSanitizer to check reads/writes around exact-size input allocations.
`-DREFERENCE_REF=6955dca` compiles the same fixtures against the original source
to reproduce the old failures.
