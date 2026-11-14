# Early client shutdown regression

`FS_InitFilesystem` can call `Com_Error(ERR_FATAL, ...)` before `CL_Init` has
registered `cl_discordRichPresence`. In a Release build with Discord enabled,
`CL_Shutdown` used to dereference that null cvar while handling the original
error. The November 13, 2026 dump from `10b9403` resolves to that condition in
`cl_main.cpp:4373`; its original error is `Couldn't load mpdefault.cfg`.

The harness compiles the actual `CL_Shutdown` body with subsystem stubs. It
checks early shutdown, Discord enabled/disabled/already initialized, repeated
and recursive shutdown, and a build without Discord. It also verifies that the
rest of client cleanup completes and releases client state. It does not start
Discord or replace an in-game lifecycle test.

```sh
cmake -S tests/client-shutdown -B build/client-shutdown
cmake --build build/client-shutdown --config Release
ctest --test-dir build/client-shutdown -C Release --output-on-failure
```

To verify the regression against an earlier source file, configure with
`-DCL_MAIN_SOURCE=/path/to/cl_main-before.cpp`. The `10b9403` source crashes in
the four null-cvar cases; the four controls pass.

This fix preserves the original startup error. A game installation that cannot
find `mpdefault.cfg` still needs its game-data files or search paths corrected.
