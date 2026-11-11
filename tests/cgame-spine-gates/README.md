# Spine correction gates

This suite compiles the actual `BG_G2ClientSpineAngles` function and its real
animation predicates against engine math and cgame types. A frozen copy of the
previous function provides the reference. Only the mocked motion-bolt service
and predicate call counters replace engine services.

The optimized function checks death, matching current/previous upper and lower
animations, and vehicle state before classifying animations. The predicates
being skipped are read-only switches, including the client server-mod check in
`BG_InKnockDown`; no animation or bolt work is dropped when correction is needed.

The fixture checks 33,080 scenarios across every animation ID, base/JA+/JAPro
server modes, special moves, current/previous mismatches, dead entities and
vehicles. Output vectors, correction state, input entity state and bolt requests
match exactly. In this fixture, 7,752 bolt requests remain unchanged while
classification calls fall from 950,589 to 542,745. This is an avoided-work count,
not a game frame-rate measurement.

Configure and run with CMake/CTest as for the other standalone suites. The
optional `-DREFERENCE_REF=383785a` selects the previous production function as
the subject; it fails the cheap-path work check. The default fixture needs no
Git history.
