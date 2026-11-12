# Client-side duel partner (experimental)

A second EJK client can act as a sparring partner: select a player under its
crosshair, continuously face them, then repeat two D attack pulses with forward
movement between them. This requires the client executable containing the
`duelpartner` command. A config file alone cannot implement target tracking.
No bot files, server module changes or modified client for the human opponent
are needed. Use this on a private training server.

## Setup

1. Run the updated EJK executable as a second client and connect it to the
   private server. Use a distinct name such as `TrainingPartner`.
2. Join play, equip and ignite a single saber, and select the intended style.
3. In the training client's console enter:

   ```text
   bind F9 "duelpartner start"
   bind F10 "duelpartner stop"
   ```

   Alternatively copy `scripts/duel-partner.cfg` into its `GameData/base` or
   active mod's config folder and run `exec duel-partner.cfg`. Loading the
   config installs bindings without starting movement. Record existing F9/F10
   bindings first so you can restore them afterward.
4. Close the console, aim at the intended player's torso, and press F9 **in the
   training client's window**. Then switch to your main client and play.
5. F10 stops it. F9 reacquires the current crosshair target and restarts it.
   `duelpartner stop` also works in the console; opening a console/menu stops
   an active run. Invalid acquisition stops the previous run too.

Only the second client is automated. The target remains the selected client
slot; the driver never switches to whoever is nearest or crosses its aim.

## Behavior and timing

The driver turns toward the target's latest snapshot torso position and repeats
the player's supplied bind at 142 client FPS:

```text
+moveright; wait 2; +attack; wait 2; -moveright; -attack; +forward; wait 350; -forward;
+moveright; wait 2; +attack; wait 10; -moveright; -attack; +forward; wait 500; -forward
```

Both attacks have a right-only lead-in of `wait 2` (14 ms at the reference pace).
Forward is released throughout both attacks: forward plus right would
select a diagonal swing in the existing saber movement code. There is no
initial approach-to-range phase, neutral recovery, or extra pause. Tracking
continues during every phase, including forward movement; it does not stop at
a distance threshold.

| Cvar | Default | Meaning |
| --- | --- | --- |
| `cl_duelPartnerWaitFps` | `142` | Reference FPS for converting the bind's waits |
| `cl_duelPartnerFirstAttackFrames` | `2` | First right+attack hold: 14 ms |
| `cl_duelPartnerFirstForwardFrames` | `350` | First forward hold: 2450 ms |
| `cl_duelPartnerPrepareFrames` | `2` | Right-only lead-in to each attack: 14 ms |
| `cl_duelPartnerSecondAttackFrames` | `10` | Second right+attack hold: 70 ms |
| `cl_duelPartnerSecondForwardFrames` | `500` | Second forward hold: 3500 ms |

Delays are `frames * (1000 / reference FPS)`, using integer division as in
the engine's frame limiter: 142 FPS means 7 ms steps. Reference FPS is clamped
to 1–1000 and wait counts to 1–10000. Holds begin with the first emitted input
and advance on the first generated command at or after their deadline. At most
one phase advances per command, so a stalled frame cannot skip an attack or
forward phase. Settings apply live and are not automatically saved. The old
range, recovery, pause and millisecond timing cvars no longer drive this sequence.
No FPS, name, style or force settings are changed.

The driver preserves this reference pace even at another rendering FPS, subject
to command-step quantization. To compare with the original bind, use 142 FPS
and `cl_cmdratecap 0`: EJK's `wait` countdown follows generated 125 Hz commands
when `cl_cmdratecap` is enabled instead of every rendered frame. Do not run the
original mouse-wheel bind simultaneously with the driver. F9 starts repetition;
the standalone mouse-wheel bind runs once per activation.

This attempts **the two D inputs in the supplied bind**, not a verified form.
The server still chooses saber moves and timing from ordinary inputs. Style,
blocks, contact, server mod and latency can change the actual attacks. Tune
against a demonstration of the desired form. The tests count input pulses,
not successful swings or hits.

Keep the training client rendering in the background. For example, set
`com_maxfps 142`, `com_maxfpsUnfocused 142`, `com_maxfpsMinimized 142`,
`cl_unfocusedChatbox 0` and `cl_minimizedChatbox 0` in that client. Record and
restore previous settings afterward. Avoid minimizing it until background
operation is checked; user commands still require the client to run normally.

## Stop conditions and limits

The driver stops if either player dies, the local player leaves normal saber
play, the target leaves the snapshot or its player info changes, the session or
map changes, snapshots become stale, or a console/menu/chat input catcher opens.
A new gamestate clears its state. Restart manually after a stop or respawn.
Stopping emits a neutral command; no synthetic held-key state or queued script
callbacks remain. Normal physical input resumes afterward.

Line of sight is checked against the world BSP. Moving doors, other entities
and Ghoul2 surfaces are not included in this trace. This is direct pursuit for
an open, flat training area, without pathfinding, obstacle/ledge avoidance,
position reset or navigation around walls. It stops on a blocked world trace.
There is no guarantee that both clients observe a brief death/disconnect before
the next snapshot. A target player-info change stops rather than silently
retargeting another occupant of the slot.

## Implementation and verification

`codemp/client/cl_duelpartner.cpp` owns commands, target checks and sequencing.
`CL_CreateCmd` applies its override after normal command construction, updating
both client view angles and encoded command angles with server delta angles
removed. Training state belongs to `clientActive_t`, which `CL_ClearState`
clears on a new gamestate. The protocol and authoritative saber code are unchanged.
`PM_SaberMoveQuadrantForMovement` in `codemp/game/bg_saber.c` is the source for
the right versus forward-right input distinction. `codemp/qcommon/cmd.cpp`
defines the frame-counted `wait` and its `cl_cmdratecap` interaction;
`codemp/qcommon/common.cpp` uses integer `1000 / com_maxfps` frame limiting.

Verified on Windows x64 with MSVC 19.51 in a Release build based on `0f2fa2f`:
the client executable and its required cgame/UI/default-renderer dependencies
built successfully. The standalone synthetic test includes the actual driver
and engine math. It checks the supplied sequence's exact movement/button order
and minimum hold times over three repeating pairs at 8/7/4/3 ms command steps,
including both right-only lead-ins, the unequal attack holds, both forward holds
and immediate repeat without a pause, plus target movement and delta angles,
stop during every phase, lost/dead targets, local death, blocked world trace,
input catchers, session/map changes, changed player info, stale acquisition,
demo rejection, stale snapshots and gamestate clearing.

Run the focused check using the repository's existing standalone-test pattern:

```text
cmake -S tests/duel-partner -B target/duel-partner-tests
cmake --build target/duel-partner-tests --config Release
ctest --test-dir target/duel-partner-tests -C Release --output-on-failure
```

In-game tracking, actual saber animation/count, background-window operation and
compatibility with the private server remain unverified. First check one pair
and F10 on an empty server you control, then test a moving target and the
intended saber style. No live server was exercised for this change.
