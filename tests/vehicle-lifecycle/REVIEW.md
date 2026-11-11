This is a source/caller review of initial findings 108-122, with two separately
reproduced vehicle defects. It does not change the project triage JSON. The
fixture executes `Board`/`Eject` and the passenger-cleanup block, not the entire
vehicle update/animation/movement system.

The caller evidence is:

- `NPC_Spawn_Do` in `NPC_spawn.c` creates and checks a fake client, sets
  `playerState`, then assigns `m_pParentEntity` before initialization. The
  vehicle points to that same entity throughout its lifetime. `_SetParent` is
  registered but has no other vehicle call site in this tree.
- `Pmove` in `bg_pmove.c` obtains the vehicle from its own player state. Its
  server driver path obtains the rider from the encoded client index; passenger
  calls explicitly require a non-NULL slot, `inuse` and `client`. `ClientDisconnect`
  retains the real client's pointer while marking it disconnected. Vehicle
  `Update` and `Animate` run before `UpdateRider`, so callback/lifetime ordering
  still matters; the passenger checks run after those callbacks.
- The actual `Eject` clears/promotes vehicle references and writes client state;
  its ordinary rider paths do not call `G_FreeEntity` or clear client pointers.
  `Ghost`/`UnGhost` check `client` before using player state. The fighter eject
  override simply delegates to the shared implementation. Freed NPC riders
  are recognized by `inuse`/`client` in ordinary update cleanup. `G_FreeEntity`
  clears the NPC entity; the top-level game loop skips freed entities. Vehicle
  death schedules `G_FreeEntity` for a later think rather than freeing its
  parent in the middle of the shared update.
- `Vehicle_SetAnim` explicitly requires `client`; animal/walker animation
  callers use the constructed parent or a present pilot. Its other callers are
  the roll/jump exits inside `UpdateRider`. This review does not execute every
  subclass animation/callback/cleanup transition.
- `G_UseTargets2` passes its trigger as `other`, and `NPC_Use` passes that entity
  to `Board`. Named vehicle spawning and `NPC_Use` callback assignment are
  present in `NPC_spawn.c`. This reachable non-client input is now rejected;
  activator identity and subsequent NPC use-response handling remain unchanged.

| Finding | Recorded operation | Evidence and remaining qualification |
| --- | --- | --- |
| 108 | Copy legs animation from `ent->client` | Parent/pilot animation and `UpdateRider` callers require a client-bearing actor. No full subclass animation or callback-mutation fixture was executed; do not mark fixed. |
| 109 | Final `Board` write to `ent->client->ps.m_iVehicleNum` | A real target-chain non-client input is now fixed at the first client read in the entry condition. The reproducer faults earlier than this recorded final write. Do not equate the new entry bug with proof that the original later-write trace itself was feasible. |
| 110 | `Update`: boarding-true parent player-state write | Parent/client construction and live-vehicle call path support the normal contract. No absent/replaced-parent whole-Update fixture; source evidence is provisional, not a fix. |
| 111 | `Update`: boarding-false parent player-state write | Same parent construction contract in the other branch. No absent/replaced-parent whole-Update fixture; not fixed. |
| 112 | `UpdateRider`: parent pointer at speed check | Driver/passenger calls supply the constructed vehicle; ordinary `Eject` retains its parent. No full stale-parent transition fixture; not fixed. |
| 113 | `UpdateRider`: parent client at speed check | The parent is the NPC client-bearing vehicle described above. Whole-UpdateRider callback transitions remain unexecuted; not fixed. |
| 114 | Roll exit writes rider velocity after `Eject` | Callers supply a driver client or check passenger client/inuse. Shared `Eject` does not free the actor or its client. Actual Eject coverage supports ordinary cleanup, but no full UpdateRider roll fixture; not fixed. |
| 115 | Dismount reads `rider->localAnimIndex` | Rider pointer comes from the checked movement callers; this branch reads animation length before eject. No complete dismount fixture; not fixed. |
| 116 | Dismount sets rider weapon time | Requires the caller's client-bearing rider. Animation-length lookup does not itself replace the actor; no complete dismount fixture; not fixed. |
| 117 | Dismount copies velocity into rider client | Same rider/client requirement; no successful eject precedes this particular copy. Full movement/callback fixture absent; not fixed. |
| 118 | Completion checks `rider->flags` | Caller supplies the rider pointer; prior successful immediate exits return from UpdateRider. Other callback ordering remains source-reviewed only; not fixed. |
| 119 | Jump exit reads parent velocity | Shared `Eject` retains the constructed parent/client; no complete jump callback/lifecycle fixture; not fixed. |
| 120 | Jump exit writes rider velocity | Known callers provide a client and shared eject retains it. The fixture checks eject ownership, not full jump continuation; not fixed. |
| 121 | Roll exit reads parent velocity | Constructed parent/client survives ordinary shared eject. No full roll continuation fixture; not fixed. |
| 122 | Roll exit writes rider velocity | Checked driver/passenger input and ordinary eject retention support the contract. No full roll continuation fixture; not fixed. |

No initial finding is automatically reclassified by this review. The target-chain
entry crash and duplicate passenger-count change have concrete before/after
fixtures; the remaining explanations above are bounded caller evidence for a
future triage merge, with their limitations retained.
