This fixture extracts the actual cgame projector, dotted-line renderer, vehicle
bracket consumer and player lead consumer from `cg_draw.c`. Only engine services
and the surrounding game state are mocked. Bullet speed/gravity helpers are
also extracted from `cg_weapons.c`; player cases use primary rockets at the real
900-unit speed. A target ten units ahead moving toward the observer at 1,800
units/second produces a lead ten units behind, while its current position is
still visible.

A visible moving target can have a predicted lead behind the camera or inside
the near-plane rejection threshold. The actual projector then returns false
without assigning either output coordinate. Both old consumers still submitted
ten dots using those uninitialized outputs. The vehicle option is enabled by
default; the player option is drawn when `cg_leadIndicator` is enabled.

The fix skips only the failed lead draw. Fourteen checks cover normal moving
targets, valid points outside the viewport, behind-camera and near-plane leads,
behind-camera target origins, empty target state and a valid player target after
a rejected one. Vehicle brackets still draw when only the lead is rejected.
All normal/off-viewport dot and marker coordinates, sizes and shaders are
checked. The projector fixture also checks that failed projections leave output
sentinels untouched, and that a point just beyond its rejection threshold works.

The pre-fix actual functions fail five consumer checks on both x64 and x86:
each invalid lead submits ten extra draws, and a mixed-target pass retains that
invalid work before the later valid target. The corrected functions pass all
fourteen checks under AddressSanitizer on both architectures. The four valid
draw-stream coordinate digests also match the pre-fix binary exactly. ASan is
used for bounds/lifetime checks; the submission-count assertions and actual
projector output contract establish the uninitialized-coordinate defect.

Configure and run normally, or supply `-DDRAW_SOURCE=/path/to/pre-fix/cg_draw.c`
to exercise an old source snapshot. No historical Git revision or game assets
are required by the default suite. This verifies behavior and avoids ten invalid
draw submissions per rejected lead; it makes no wall-clock or general FPS claim.
