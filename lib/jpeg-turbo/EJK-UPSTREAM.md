Unmodified libjpeg-turbo 3.1.4 source distribution:
https://github.com/libjpeg-turbo/libjpeg-turbo/releases/tag/3.1.4

Archive: libjpeg-turbo-3.1.4.tar.gz
SHA-256: e23d3ebb2c6ee4d0e2a5823dbb55b614845df5ea3435c956fed5cf04041a87ad

EJK builds only the static libjpeg API through cmake/BuildTurboJPEG.cmake.
No download, SDK or extra runtime DLL is required. NASM enables SIMD on x86
and x86-64; upstream uses a portable fallback when the assembler is unavailable.
Arm SIMD is selected by upstream. See LICENSE.md for the included licenses.
UseTurboJPEG=OFF retains the previous bundled JPEG 9a implementation.
