# zlib-ng

Source: https://github.com/zlib-ng/zlib-ng

Release: [2.3.3](https://github.com/zlib-ng/zlib-ng/releases/tag/2.3.3)

Pinned revision: `12731092979c6d07f42da27da673a9f6c7b13586`

License: zlib license, preserved in [LICENSE.md](LICENSE.md). Each source file
retains its upstream copyright and license notices.

This directory contains the unmodified CMake build, C sources, headers,
templates, architecture implementations and build helper files needed to build
the static library. Upstream tests, benchmarks, contributed applications and
unrelated build systems are omitted. No configure-time download is required.

EJK's integration lives in `../zlib/BuildZlibNG.cmake`; upstream files are
unmodified. It builds the zlib-compatible API as a static library with runtime
CPU detection enabled and host-native instructions disabled. IPO/LTO is disabled
for this library in every configuration, including when EJK itself uses Release
LTO, to preserve upstream's separation of runtime-dispatched instruction sets.

`UseInternalZlib=ON` and `UseZlibNG=ON` select this backend with CMake 3.14 or
newer and a supported compiler. CMake 3.10–3.13 and unsupported compilers retain
the classic bundled zlib backend with a configure diagnostic.
`UseZlibNG=OFF` explicitly selects classic bundled zlib;
`UseInternalZlib=OFF` continues to select the external zlib package.

The game continues to build its modified bundled minizip and bundled PNG code.
No additional runtime library is installed. The zlib-ng license is installed in
the game's `licenses/zlib-ng` directory when this backend is selected.
