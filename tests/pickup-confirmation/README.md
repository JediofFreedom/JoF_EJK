# Confirmed capacity-limited pickups

This branch replaces the unconditional prediction suppression from #184 with
an opt-in client/server path. It requires **both** updated modules. This repo's
server is JAPro-derived; a separately maintained JA+ server must port the change.

## Protocol v1

- Server advertises `g_pickupConfirm=1` in serverinfo (ROM).
- Client advertises `cg_pickupConfirm=1` in userinfo (ROM).
- After an accepted health, armor, ammo or holdable pickup, the server sends
  the collector `jof_pickup <item-modelindex>` on the reliable command channel.
  Rejected touches send nothing. Item indices use the existing shared item table.
- The client disables speculative pickups for those types only on a supporting
  server, and suppresses its duplicate ordinary pickup events. The reliable
  command invokes sound/console feedback once, without the entity-based 500ms
  suppression. Command sequence handling already executes each reliable command
  once; two separate commands for the same item type are two real pickups.
- Ordinary events are retained for other players and legacy clients. Legacy
  servers retain the original client prediction behavior. Weapons, flags and
  powerups keep their existing handling.
- HUD notifications have a 16-entry pending queue, advancing no faster than
  every 750ms. Overflow drops the oldest pending icon, not console/sound feedback.
  The last icon retains the normal three-second fade. Map restart clears the queue.

Do not deploy only the client and expect legacy JA+ pickup prediction to change.
Do not combine this with the separate `fix/confirmed-pickup-feedback` snapshot
workaround: remove that workaround when integrating into a branch containing it.

## Automated checks

```text
cmake -S tests/pickup-confirmation -B build/pickup-confirmation-check
cmake --build build/pickup-confirmation-check --config Release
ctest --test-dir build/pickup-confirmation-check -C Release --output-on-failure
```

The harness compiles extracted production server confirmation, capability,
command parsing and HUD queue functions. Engine transport and the final
sound/console callback are mocked. It checks shield + medpack, distinct repeated
pickups, malformed commands, version mismatch, spectator filtering, and queue
bounds. It does **not** simulate a live network or prove audibility/rendering.

## Required live checks

1. Run the updated server module and client; verify both capability cvars are 1.
2. At low health/armor, collect a shield and medpack together: verify both stats,
   two console lines and feedback sounds, and the two icons displayed in sequence.
3. Near capacity, touch several shields or medpacks: feedback must match only
   accepted pickups. Test two real pickups of the same type within 500ms too.
4. Repeat under latency/loss, across death, reconnect and map restart.
5. Test updated client on old server and old client on updated server; neither
   should get unknown commands or lose its normal pickup feedback.
6. Verify weapon autoswitch, flags, dropped items and spectator behavior.

Reliable confirmation avoids reliance on the two-entry snapshot event ring,
but introduces confirmation latency and shares the engine's finite reliable
command buffer. Pathological pickup spam can overflow that buffer, as with
other reliable server messages. This is not a wire-protocol expansion.
