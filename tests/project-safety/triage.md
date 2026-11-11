Audit step: non-renderer engine and game-interface safety (2026-11-11).

Confirmed and fixed, with production-code regression fixtures:

- `Com_Filter`: `?` advanced beyond empty/short names, and a matched character
  class without its closing bracket advanced beyond the pattern terminator.
  The `*` literal path also copied arbitrarily long literals into a fixed stack
  buffer. Matching now checks bounds and searches the existing literal range
  directly, avoiding that copy and repeated literal-length scans. Prefix-match
  semantics and existing case-sensitive/insensitive matching remain covered.
- `SCR_DrawDemoRecording`: a negative `cl_drawRecording` value used an
  uninitialized display string. Nonpositive values now disable the indicator.
  The optional FPS indicator also skips zero-duration frames instead of dividing
  by zero. Existing positive indicator modes have explicit expected output.
- `SV_SvEntityForGentity`: listen servers and the dedicated JAPro exception
  logged invalid/null game entities but continued into pointer/array access.
  Invalid entities now take the existing `ERR_DROP` recovery path on every
  build. Valid ordinary and legacy entity mappings have explicit fixtures.

Validation: all three CTest cases pass with MSVC x64 AddressSanitizer. The same
fixtures against commit `6955dca` fail all three cases; the server-entity fixture
reproduces an AddressSanitizer null-access crash. This is a focused review, not
proof that every first-party subsystem is free of defects.

Additional candidates reviewed but not changed in this step:

- `CNavigator::Load` leaked the newly created node when `CNode::Load` rejected
  its header and retained earlier nodes on partial failure. A subsequent audit
  step fixed ownership, bounded all table reads/counts, and batched edge/rank
  reads. That work and compatibility fixtures live in `tests/navigation-loading`.
- `CM_PatchCollideFromGrid` warnings require checking the grid dimension and
  wrapping invariants across subdivision; the warning alone does not establish
  an uninitialized read on a valid generated grid.
- Several `CL_ParsePacketEntities` null warnings require an impossible entity
  number or continuation after the engine's `NORETURN Com_Error`; no behavior
  change was made solely to silence those paths.
- Bot chat's two-pass allocation/parser warnings and Icarus token out-parameter
  warnings need individual input fixtures before deciding whether they are
  real failures or limitations of cross-function analysis.

The full production-context analyzer output and coverage accounting are managed
by the orchestrator in `tests/project-audit`; a parse failure is not a clean
analysis result.
