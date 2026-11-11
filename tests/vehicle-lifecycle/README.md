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

The initial twelve production-path checks cover map activation with/without an activator,
valid pilot/passenger boarding, disconnected/non-client/freed/dead riders,
multiple passengers, repeated cleanup, blocked race exits, solid-blocked exits,
pilot promotion and empty occupancy after the pilot leaves. Both x64 and x86
AddressSanitizer runs pass. The saved pre-fix source fails nine cases, including
two actual target-chain NULL reads. Valid boarding and pilot-promotion state
summaries match before/fixed binaries.

The sparse-seat follow-up expands the suite to 31 checks. It extracts the full
actual `AttachRiders` / `AttachRidersGeneric` functions and the verbatim server
passenger loop from `Pmove`. That loop's callback records the original rider,
seat and command pointer, or invokes the actual `Eject` to exercise count changes
during iteration. The full movement/animation callback is not substituted into
this harness. The actual `BG_VehicleClampData` and `VEH_MAX_PASSENGERS` define are
also extracted: default and named vehicle parsing already clamp capacity to
0..10, and the new scans additionally enforce the fixed array limit.

An occupied-seat count is not a valid array prefix after an eject. Manually
initialized slot-1/count-1 and last-slot/count-1 arrays reproduce the old bug
independently of the previous count fix. Tests also cover dense/full/empty
arrays, a lower configured capacity with a poisoned out-of-range slot, actual
eject/pilot-promotion transitions and movement callbacks that eject all
passengers. Attach/update leave seat pointers, `generic1`, bolt selection and
existing valid coordinates/state intact; only pilot promotion deliberately
changes the selected passenger's role. Existing dense promotion rearrangement
behavior is unchanged.

All 31 cases pass under ASan on x64 and x86. The post-counter-fix `1e8a5a5`
consumer baseline passes the initial twelve checks but fails twelve new sparse,
promotion and count-changing movement checks. The before/fixed source selectors
allow these separate baselines to remain distinct; no general FPS gain is
claimed.

Saved CTest output also verifies that the eight valid boarding, pilot-promotion,
dense/full and empty state summaries match the pre-consumer-fix binaries exactly
on both architectures. The corresponding attachment checks validate every
passenger's coordinates and the unchanged production `*driver` bolt selection.

The `EjectAll` follow-up expands the suite to 58 checks and extracts the actual
`EjectAll` and `G_EjectDroidUnit` bodies as well. A pilot eject can promote an
original passenger into the pilot role; iterating only the remaining passenger
slots then leaves that promoted rider aboard. The cleanup now snapshots the
original pilot, old pilot and configured seats in that order, deduplicates the
fixed list of at most twelve actors and attempts each still-associated actor
once. A callback that already detached a queued actor is respected. Successful
pilot ejection also clears an old-pilot reference only when it aliases that
exact ejected actor, preventing a second eject/damage of the same rider.

`Eject` remains the sole owner of slot/count changes. Race-policy and blocked
living-rider exits remain refused, with occupied slots and counts intact;
`EjectAll` reports false while occupants remain. The existing callers ignore
that result. Forced dead-rider exits, damage requested by `killRiderOnDeath`
(including when exit policy refuses an attempt) and droid cleanup retain their
existing behavior. Damage skips an entity already freed by a callback.

The 27 additional checks cover empty/no-pilot/pilot-only vehicles, dense and
sparse seats, all ten seats plus a distinct old pilot, promotion chains, aliased
pilot references, refused exits with/without death damage, callbacks that reject
or do not remove a rider, nested ejection, and callbacks that free or detach a
later queued rider. Detached actors reused for unrelated entities are skipped
before dereference. Call order, one attempt/damage per original actor, forced
exit arguments and coherent slots/counts are checked. These are stable entity
arena tests; they do not claim a general entity-generation lifetime guarantee.

All 58 cases pass under ASan on x64 and x86. The post-sparse-fix `a0e287c`
baseline passes all 31 earlier checks and five new valid cleanup cases, but
fails 22 new cleanup checks. Existing valid state summaries, including the
already-correct empty, pilot-only, no-pilot and droid cleanup cases, match the
saved pre-cleanup-fix output. This is a lifecycle correctness fix; no measured
FPS benefit is claimed.

Set `VEHICLE_SOURCE` and `PMOVE_SOURCE` to saved pre-fix `g_vehicles.c` and
`bg_pmove.c` files to reproduce old consumer failures. The default suite needs
no historical checkout or game assets. It
does not execute the full `UpdateRider`/movement/animation lifecycle and does not
automatically resolve the other initial vehicle analyzer findings; see
[REVIEW.md](REVIEW.md) for the per-finding evidence and limits.
