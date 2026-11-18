# Sound submission diagnostics

`s_runtimeProfile 1` enables five aggregate frontend stages in `S_StartSound`:
sample load, cache touch/event clock, rate limiter, channel selection and channel
setup. The default is 0 and reads no profiling clock. Use
`s_runtimeProfileReset` / `s_runtimeProfileReport`; sound shutdown reports any
collected counters automatically. The profiler uses `steady_clock`, so it does
not itself call the event-pumping `Com_Milliseconds` clock.

The standalone fixture compiles the actual software sound entry point and
production SFX, channel and MP3 types. Mock services verify unchanged order and
channel fields for cached/new PCM, MP3, limiter rejection, unavailable channels,
muting and errors. It checks zero disabled clock calls, balanced stages across
returns/exceptions and rejection of pre-reset marks. Backend timings are
diagnostics; instrumentation overhead is included, and they do not establish a
frame-rate improvement.
