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

Dispatch is also grouped into sound (including sound-channel events), footsteps,
effects, saber swing/spin and other events. The type switch runs only when the
fine profiling mark is active. Groups reuse the dispatch start mark; the total
also includes recording the group counter, so instrumentation overhead makes it
slightly larger than the sum of group times. Event selection, RNG and actual
dispatch behavior remain unchanged.

Footstep substages measure bolt/matrix work, water and ground traces, sound,
effects and footprint marks. The fixture compiles the actual two-function
footstep chain and checks ordered service calls, foot selection, trace geometry,
material sounds and mark parameters across disabled/class-excluded, water,
missing-ground, dirt, metal, sound-only and debug-mark paths. Immediate scope
ends keep early-return counters balanced; instrumentation adds no caches or
changes to footprint/audio/effect behavior.

The fixture exercises the actual player wrapper, actual sound-trigger function
and actual animation call site with mocked bone/event/playback services. It
checks normal frames, unchanged frames, the no-lumbar return and bone-query
failure while verifying balanced counters. Other cases cover disabled clocks,
async load ownership, nested stages, stale marks and reset/shutdown handling.
