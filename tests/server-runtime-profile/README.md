`sv_runtimeProfile 1` enables four fixed engine-private server counters.
`sv_runtimeProfileReset` clears them; `sv_runtimeProfileReport` prints aggregate
call counts, total milliseconds, mean microseconds per call and maximum
microseconds per call. It defaults to `0`, is not archived, and respects an
explicit startup value. Disabled scopes read no clocks or allocate memory.
Enabled scopes use `std::chrono::steady_clock`, without event-pumping clock
helpers, per-frame printing or heap allocation. State and commands belong to
the existing server/main thread.

| Report stage | Scope and denominator |
| --- | --- |
| `sv-frame` | An eligible `SV_Frame` after stopped/paused checks, including AI, tick catch-up and normal message sending. Listen-server calls may run no game tick. |
| `bot-ai-call` | One actual `SV_BotFrame` callback after enabled/game-started checks, for both listen and dedicated servers. |
| `game-tick` | One `GVM_RunFrame` call, including native/legacy dispatch. A catch-up frame may contain several ticks. |
| `client-messages` | The normal `SV_SendClientMessages` call block, including any snapshot construction, fragments and network submission it performs. It does not include all other network/event handling. |

Stages overlap; their totals must not be added together. These are diagnostic
times with profiling overhead, not a general server-performance or FPS claim.
Player input processed outside `SV_Frame`, packet-event handling, the whole
client frame and rendering are outside the whole-server-frame scope.

Game shutdown automatically reports populated counters before map change, fast
restart or VM unload. Server shutdown flushes any remaining counters before
cleanup. Initialization and these boundaries reset the generation. An outer
scope spanning reset/shutdown cannot publish old timings into the new map.
Commands remain registered across server stop/restart, like existing server
operator commands. Repeated empty shutdown does not print another report.

The 22-case fixture compiles the production profiler and extracts actual
`SV_Frame`, `SV_BotFrame`, `GVM_RunFrame`, bot/native/legacy dispatch, game
initialization/shutdown, fast restart, VM unbinding and server-shutdown bodies.
Only engine services/state and the clock are supplied by the harness. A
chrono-compatible deterministic clock counts every clock access. Checks verify
zero disabled reads, identical ordered engine/game callbacks with profiling
off/on, listen/dedicated and native/legacy paths, multiple tick catch-up,
stopped/paused/no-VM/no-bot/no-game returns, deferred restart, command reset and
report, map changes, repeated/error shutdown, exceptions, mid-frame reset and
toggles, and an explicit startup setting. All 22 pass with ASan on x64 and x86.

For a later live diagnostic, use a fresh local profile on `mp/ffa3`,
`sv_maxclients 32`, `bot_enable 1`, `g_newBotAI 0`, `sv_fps 40`,
`sv_hibernateTime 0`, no frag/time limit and 24 stock bots with unique alternate
names (`addbot Alora 3 free 0 audit-00`, cycling through known installed bot
definitions). Leave AI, movement and ordinary gameplay active; do not reuse the
idle-crowd recording's freeze setup. Connect a local observer so actual client
message submission also occurs. Check population and active movement/combat,
warm up, reset, then collect a fixed interval and report. Keep observer/client
limits and server settings identical across rotated runs. A bot workload does
not represent the network cost of 24 human clients. Demo playback cannot measure
this live server simulation; no live measurements are claimed by this fixture.
