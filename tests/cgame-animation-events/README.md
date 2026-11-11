# Indexed animation event candidates

The measured 24-player replay spent about 0.116 ms per client frame scanning and
dispatching animation events, versus 0.0067 ms querying bone frames. This change
indexes event keys when an event file finishes loading. Playback still applies
the original matching, probability and dispatch code in source-array order.

Each torso/leg catalogue stores at most 300 sorted 16-bit source slots, a count
and a validity flag: 152 KiB for all 128 supported model sets. A query uses a
40-byte slot bitmask plus a cursor. Exact-frame queries and the legacy +/-3
proximity windows select a superset of possible matches. Wider grapple handling
retains the original scan. Unbuilt or invalid indexes also use the original scan.

`ParseAnimationEvtBlock` invalidates before changing keys/types. Outermost
`BG_ParseAnimationEvtFile` completion rebuilds after includes/stomps, including
missing files; cached parser hits rebuild invalid metadata. `BG_InitAnimsets`
invalidates every set. Runtime deferred effect work changes only event data and
string contents, so it keeps the index valid. No played-event state is cached.

The suite compiles the actual selector, matching predicates and parser. A frozen
pre-index production selector supplies the reference without requiring Git
history. Mock dispatch records original slots, event data, order, RNG arguments
and results, and deferred metadata mutations. It covers 8,007 cases, including
unsorted and duplicate keys, all event types, legacy `doEvent` carry, the first
`AEV_NONE`, full/empty arrays, skipped/reversed/looped frames, grapple animation
transitions, invalid indexes and widened query bounds. Actual parser checks cover
includes, overwrites, cache hits, filename reuse, missing files, initialization
and replacement at the same address.

x64 ASan and x86 Release checks passed. In this fixture, visited catalogue slots
fell from 1,167,727 to 63,661 while all 11,778 dispatches and RNG/state results
matched. This is an avoided-work count, not a game frame-rate measurement.
`-DREFERENCE_REF=79b5c52` selects the earlier selector as the subject and fails
the avoided-work assertion.
