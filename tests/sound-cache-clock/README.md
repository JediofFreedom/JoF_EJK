# Sound cache clock

The measured 24-player replay made 21,144 sound-cache touches. They spent
452.053 ms in the old event-pumping clock, about 90% of sound submission time.
On Windows that path reaches `CON_Input` / `GetNumberOfConsoleInputEvents`;
it does not pump SDL input.

`SND_TouchSFX` now reads `Sys_Milliseconds` when journaling is initialized and
off. Recording, playback and uninitialized journal state retain the previous
`Com_Milliseconds` call. Cache time still uses the same system-clock time base,
`+1` convention and media level. Touch remains before rate-limit rejection.
No filesystem provider, sample data, audio settings or cache ownership changes.

The fixture compiles actual production `Com_Milliseconds`, journal event
reading/writing, pushed/native event queues, sound touch, PCM/MP3 freeing, LRU
eviction and level-cache cleanup. Only clock, console, file storage, renderer
level and allocation services are mocked. A frozen pre-change touch supplies
the reference without requiring Git history.

Checks cover 5,000 normal touches with zero event pumps; identical timestamps,
eviction order, protected/default/in-use samples and level lifetimes; queued
key/character/mouse/console delivery and mouse coalescing; byte-identical journal
recording and matching replay reads/calls/event order; native console ownership
at the unchanged main event consumer; reset, uninitialized-service fallback and
defined signed-clock boundary values. The pre-existing `INT_MAX + 1` timestamp
case is outside this change and is not treated as defined arithmetic.

`-DREFERENCE_REF=6d9c5f4` selects the earlier production touch as the subject and
fails the zero-pump work assertion. Timing improvements require an actual-game
comparison; fixture work counts are not a frame-rate claim.
