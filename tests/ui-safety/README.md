Configure this directory and run CTest. The fixtures compile the production map
and server preview functions plus the actual integer parser and cvar-int item
parser. Engine renderer/token services are supplied by the test harness.

Cases cover existing and missing preview images, switching between them, wide
fallback art and its missing-image fallback, empty catalogs, the index at the
catalog boundary, valid signed limits, missing parameters, and malformed numbers.
They check that valid image/layout choices remain unchanged and rejected cvar
definitions do not overwrite numeric limits. The menu-item fixture also compiles
the real UI arena allocator and item initializer, checking successful creation,
pool exhaustion, failed item parsing, and full menus without changing item counts.
AddressSanitizer is supported.
`-DREFERENCE_REF=6955dca` runs identical fixtures against the old functions.
