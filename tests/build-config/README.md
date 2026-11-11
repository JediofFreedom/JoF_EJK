Configure this directory and run CTest. The test exercises the production MSVC
compiler configuration twice in the same build directory for both runtime
choices. It checks stable flags, unmodified cache inputs, and preservation of
language-specific C/C++ toolchain settings. It runs without an MSVC installation.
