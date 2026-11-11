`triage.json` accounts for the initial analyzer's 115 diagnostic compiler
contexts and 349 warning appearances. Exact duplicates across contexts become
285 findings, retaining each original context identifier. The deduplication key
includes the source path, line, column, check and message.

Every finding has a disposition, rationale and source/test evidence. Source
locations and recorded statements refer to the initial diagnostic trace; line
numbers may move as fixes land. Context summaries stay unresolved if any of
their findings remain unresolved. A context containing fixes and provisional
contract explanations retains those separate finding dispositions.

The review examined every reported statement and nearby source. Specific
allocator, error, aliasing and ownership contracts were followed where cited.
This is not a complete caller-path proof for every finding, and the JSON keeps
those cases unresolved. A likely contract/assert false positive is provisional;
it is neither a production fix nor permission to ignore a future reproducer.

Confirmed fixes cite regression fixtures. Unresolved entries include additional
high-confidence candidates awaiting fixtures and lower-confidence warnings
requiring valid runtime state. Dependencies such as bundled MikkTSpace remain
identified explicitly. Parse failures and timeouts belong in the overall audit
coverage ledger; they are not clean translation units.

The Icarus metadata update marks five findings in the actual `Get`, `GetVector`
and `Move` paths as fixed, citing `tests/icarus-task-parameters`. Those fixtures
pass on x64 with AddressSanitizer and on x86, and fail against the baseline.
Sixteen other Icarus command-consumer findings retain unresolved status because
this step did not execute those commands or confirm their post-fix diagnostics.
Sharing the corrected readers alone does not give every Icarus finding fixed
status.

The initial ROFF playback update marks finding 54 as fixed: dedicated-client ApplyROFF
and ClearLerp now reject unsupported playback before dereferencing null
trajectories. Separate production-method fixtures reproduce both null writes
against baseline 8cc641a. The same suite also reproduces client buffer lifetime,
translated-client angle initialization and overlong note-copy defects discovered
during this review; those new defects are not added to the initial 285-warning
count. Those eight tests pass with x64 AddressSanitizer and on x86 Release.

The ownership follow-up adds actual InitROFF2-to-destructor and Play-to-Restart
fixtures. They reproduce a new[]/scalar-delete mismatch and retained playback
records against the baseline. The expanded playback suite passes 10/10 on x64
with AddressSanitizer and on x86 Release; neither extra defect changes the
initial diagnostic count.

Client callback findings 181 and 182 are now fixed, citing
`tests/roff-client-notes`. The actual callback previously read beyond a local
string terminator after incomplete offset or angle components. Sanitizer
fixtures poison the unused local string tail to expose those reads; five other
fixtures reproduce real buffer overflows without that instrumentation. All nine
checks pass on x64 with AddressSanitizer and on x86 Release. Valid effects,
sounds, loop notes, signed offsets and space-separated offsets also pass against
the baseline. Other findings remain at their existing dispositions.

Lead-projection findings 177 and 180 are now fixed, citing
`tests/cgame-lead-projection`. The actual cgame projector leaves its outputs
untouched when a moving target's predicted lead crosses behind/inside the view
plane, but both vehicle/player consumers submitted ten dots with those
uninitialized coordinates. Each failed projection now skips that lead draw;
vehicle brackets, later player targets and valid off-viewport projections retain
their behavior. All fourteen actual-projector/consumer checks pass under
AddressSanitizer on x64 and x86. Five checks fail against the saved pre-fix source,
whose hash matches the fresh targeted analyzer input. Four valid draw-stream
coordinate digests match the baseline exactly on both architectures. The
submission-count assertions and projector output contract establish this defect;
ASan is used for bounds/lifetime checking. Only these two finding dispositions
change, leaving 46 confirmed fixes, 59 provisional contract explanations and 180
unresolved candidates among the original 285 findings.
