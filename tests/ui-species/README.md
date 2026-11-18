Standalone CMake/CTest regressions for the lazy species catalog. Production
lifecycle, character-cvar, multi-choice parser/display/key, and feeder-count code
is compiled against a counted catalog-building service. Tests verify no build
during startup/map initialization or closed-menu parsing, one build before
consumer use, preserved model/skin selection, cached repeated access, refresh
after invalidation, safe empty catalogs, and repeated shutdown.

The CMake fixture checks that UI_Init continues to use the tested no-scan paths.
`ui_lazyPlayerSpecies 1` (default) enables lazy initialization; `0` provides the
previous eager behavior for controlled comparisons with the same engine/UI binary.
Both modes have counted initialization/selection fixtures.

Actual script dispatch and menu activation also exercise the automatic basic
profile menu without catalogue reads. Customization controls trigger a build
before use, even while a different menu retains focus during onOpen. All species,
skin and color feeders, nested script context restoration, and the current custom
model/skin selection are covered.

Real game profiling and the player-customization smoke test validate the actual
catalog builder and menu rendering; no test asserts wall-clock timing.
