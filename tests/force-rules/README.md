# Force allocation regression checks

Standalone test target (does not require the engine or Boost):

```text
cmake -S tests/force-rules -B build/force-rules-check
cmake --build build/force-rules-check --config Release
ctest --test-dir build/force-rules-check -C Release --output-on-failure
```

The harness extracts the production synchronization helper, UI cost helper,
force-cost tables and shared loadout validator. Engine cvars/serverinfo are
mocked. It checks map/rule transitions, restart initialization, preservation of
explicit server overrides, duel weapon rules, and 20,000 generated loadouts,
including fully spent allocations. It is not an in-game networking/UI test.

The reconnect regression also executes production `CG_Shutdown` and
`UpdateForceUsed` with cleanup/menu imports mocked. It checks a 100-point
light-side allocation (Jump, all light powers, Saber Offense and Defense at
rank 3, Push at rank 1), a UI refresh between shutdown and reconnection, and
connection to a different server with paid saber ranks.

The UI initialization check uses the registration/load order extracted from
`UI_Init`, with production `UI_UpdateForcePowers`, `UpdateForceUsed` and
`UI_UpdateClientForcePowers`. It reproduces the screenshot's saved request
changing from `7-1-330103000333000330` to `7-1-330103000333000320` under
the old load-before-registration order, then verifies the current order
preserves Defense rank 3 through the menu's save operation. Engine cvars and
the UI's initially zeroed cvar mirror are represented separately for this test.

## Live verification

Use matching updated UI and cgame modules. The existing engine-side
`forcechanged`/userinfo ordering fix should also be present.

1. On Public, run `/forceinfo` before changing the loadout on each affected map.
   Compare the advertised weapon rules, `ui_freeSaber`, and requested loadout.
   Matching max rank and force-disable settings alone
   does not establish that the saber costs should match.
2. Repeat with a fully spent loadout, apply it, respawn, and run `/forceinfo`
   again. Confirm the requested ranks stay unchanged, and test powers in-game.
   The protocol does not transmit full applied ranks; `/forceinfo` cannot
   verify server-applied Saber Offense/Defense from the snapshot.
3. Test paid-to-free and free-to-paid saber rules, map changes, reconnects and
   `map_restart`. Repeat after changing a serverinfo setting unrelated to
   weapons, checking that explicit `EV_SET_FREE_SABER` overrides survive it.
4. Confirm normal Force editing with both `ui_freeSaber` values. Rank-one
   Offense/Defense are free only in free-saber mode; higher ranks keep their
   ordinary costs.
5. For the separate devmap stance/attack report, record `/forceinfo` while
   playing as yourself (not following), before and during the lock. Include
   the map, server mod, saber models and reproduction steps. Requested offense rank,
   holster state, weapon timer, hand extension and saber lock state are
   relevant; the synchronization patch does not bypass gameplay restrictions.

`ui_freeSaber` is now runtime state rather than an archived preference. Cgame
seeds it from the advertised saber-only rules on initialization/restart and
updates it when the rule inputs change. Explicit server events still override
that default. A mod that does not advertise its rules still needs to send its
free-saber event; the client cannot infer an unadvertised custom rule.

Shutdown deliberately retains the last known costs until the next gamestate
replaces them. Resetting them during teardown can irreversibly trim a rank
from the UI allocation before the actual server rules arrive.
