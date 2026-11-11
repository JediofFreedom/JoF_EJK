The actual `CG_ClipMoveToEntities` is compared with its immutable pre-optimization
production body from `8cc641a` (`baseline-clip.h`). Fixtures compile real cgame
and collision types, the vehicle bbox adjustment, trajectory evaluation, collision
trace setup and transforms, box-hull setup, brush tests and plane clipping.
Only the fixture's one-brush leaf/model lookup and Ghoul2 service are supplied by
the harness; no BSP/world tree or actual Ghoul2 mesh is loaded.

Every returned trace field and the winning entity must match exactly. Coverage
includes 6000 deterministic randomized sweeps, points, asymmetric/null extents,
reversed/stationary sweeps, startsolid/allsolid, epsilon-adjacent edges, rotated
brush models, ignored entities, existing world hits, content masks, vehicle bbox
overrides and orientation restoration, Ghoul2 rejection, duel/race filtering,
noncanonical packed solid values and inverted extents.

A counted 64-solid workload requires 64000 baseline transformed traces to become
1000 in 1000 sweeps, with identical results. This measures avoided collision work,
not a general FPS claim. Configure with `-DREFERENCE_REF=8cc641a` to compile the
old subject function and reproduce the work-count failure. The default subject
and collision/vehicle functions are extracted from current production sources.
