# Cgame frame profiling

`cg_runtimeProfile 0` is the default. Level 1 records coarse frame stages;
level 2 adds aggregate entity/player stages, shadow/water work and animation
sub-stages. Fine scopes read no clocks at levels 0/1. Reports contain owning
thread elapsed times, and nested totals overlap. Instrumentation changes timing;
use the uninstrumented replay for comparative frame-rate measurements.

The four animation sub-stages separate sound-trigger processing, animation
playback, bone-frame queries and event scans/dispatch. Bone queries and event
processing are nested inside the inclusive sound-trigger stage. Reports include
call counts so unchanged sampled frames and models without a lumbar bone can be
distinguished from timing changes.

Indexed and linear selector stages report actual branch usage. The dispatch
stage is nested inside either selector stage; comparing it with the inclusive
totals separates catalogue lookup from sound/effect execution. The branch
fixture compiles the actual selector and grapple predicates, covering a valid
index, an invalid index, grapple fallback and an empty catalogue at coarse/fine
profile levels. These diagnostics do not change index validity or event state.

The fixture exercises the actual player wrapper, actual sound-trigger function
and actual animation call site with mocked bone/event/playback services. It
checks normal frames, unchanged frames, the no-lumbar return and bone-query
failure while verifying balanced counters. Other cases cover disabled clocks,
async load ownership, nested stages, stale marks and reset/shutdown handling.
