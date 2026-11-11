This suite extracts the actual shared `Board`, `ValidateBoard`, `Eject`,
`SetPilot`, ghosting and occupancy functions, and the verbatim passenger cleanup
block from `Update`. It also runs the actual `G_Find` / `G_UseTargets2` /
`GlobalUse` / `NPC_Use` chain. Engine services and surrounding entity state are
mocked; boarding, eject policy, slot removal and passenger count logic are real.

A named spawned vehicle receives `NPC_targetname` as its targetname and
`NPC_Use` as its use callback in `NPC_spawn.c`. Ordinary target activation passes
the trigger as `other` and the initiating player separately as `activator`.
`NPC_Use` forwards `other` to `Board`; a trigger has no client, so the old entry
condition dereferences NULL. `Board` now rejects that non-client request before
changing state. It does not silently board the activator or change map-use
semantics. Direct player boarding and normal passenger capacity remain intact.

The old `Update` passenger cleanup decremented the count after calling `Eject`,
although `Eject` already owns removal and count changes. This could make the
count negative, leaving the actual `Inhabited` predicate true after the last
pilot left an empty vehicle. It also reduced the count when the existing
swoop-race policy rejected eject and left the slot occupied. The caller now
leaves both slot and count to `Eject`; failed ejects retain both coherently.

Twelve production-path checks cover map activation with/without an activator,
valid pilot/passenger boarding, disconnected/non-client/freed/dead riders,
multiple passengers, repeated cleanup, blocked race exits, solid-blocked exits,
pilot promotion and empty occupancy after the pilot leaves. Both x64 and x86
AddressSanitizer runs pass. The saved pre-fix source fails nine cases, including
two actual target-chain NULL reads. Valid boarding and pilot-promotion state
summaries match before/fixed binaries.

Set `VEHICLE_SOURCE` to a saved pre-fix `g_vehicles.c` to reproduce the old
failures. The default suite needs no historical checkout or game assets. It
does not execute the full `UpdateRider`/movement/animation lifecycle and does not
automatically resolve the other initial vehicle analyzer findings; see
[REVIEW.md](REVIEW.md) for the per-finding evidence and limits.
