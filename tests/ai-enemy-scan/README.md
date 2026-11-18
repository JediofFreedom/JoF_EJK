This suite executes the actual `ScanForEnemies` against its unchanged production
body from `6d9c5f48f338f52874a26e2e7e1266b17f28e2c4`, retained in
`baseline-scan.h`. The subject is extracted from current `ai_main.c` at configure
time. `AI_SOURCE` can select a saved pre-optimization file to reproduce the
avoided-work failures; ordinary test runs need no historical checkout.

The actual `PassStandardEnemyChecks`, `PassLovedOneCheck`, `BotPVSCheck`,
`BotCanHear`, `OrgVisible`, `InFieldOfVision`, `AngleMod` and `vectoangles` bodies
are extracted too. Entity/client state and engine PVS, trace, team and mind-trick
services are supplied by the fixture. The actual capacity define is used,
including the existing scan's inclusive last index. This fixture does not run
the complete AI, pathfinding or combat loop.

Fifteen checks compare the exact winner, unchanged bot/entity/client state, RNG
state and ordered calls to team, PVS, love, FOV, mind-trick, hearing and collision
services. The extracted scan/services have no RNG calls; the fixture verifies
that both paths leave the RNG unchanged. Coverage includes 12,000 deterministic
randomized scans across both AI modes and gametypes, null/invalid/disconnected
actors, race/spectator/duel filters, fallback, friendly fire, Jedi Master
priority, current-enemy hysteresis, health weighting, mind tricks, recent sound
events, equal distances, reversed ordering, FOV boundaries, vertical/zero
vectors and nonfinite distances.

The conversion's only consumer is the legacy AI's FOV condition. It now runs
only when that condition can be reached, after any Jedi Master distance override.
No angle conversion is needed by the newer AI. Distance values, candidate
ordering, comparisons and all gameplay/service calls retain their original
behavior; no cadence, AI defaults, protocol or visual settings change.

A counted synthetic workload moves 24 actors through 100 layouts and scans from
each actor in each AI mode. It verifies identical results and service calls while
reducing legacy conversions from 55,200 to 12,352, and new-AI conversions from
55,200 to zero. The randomized workload reduces 31,484 conversions to 14,081.
These are avoided math calls, not measured server-frame or whole-game speedups.

All fifteen tests pass on x64 and x86 with AddressSanitizer. Selecting the actual
saved pre-optimization source fails six work-count assertions while the
selection/state/service comparisons remain identical.
