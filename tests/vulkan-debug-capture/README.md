# Vulkan debug geometry and capture

Compiles production normal/polygon drawing, screenshot/levelshot/video callers,
readback alignment and gamma-table functions. Debug tests use the production
capacity constants and compare every resulting triangle/line endpoint, color,
closing edge and adjacent-array canary across batch boundaries.
The stageVars declaration is extracted from `DEBUG_SOURCE_DIR/tr_local.h`;
`DEBUG_LAYOUT_SOURCE` can explicitly select the matching header when a saved
snapshot contains only the tested implementation files. This keeps normal and
shadow-expanded color capacities consistent with the selected production code.

Capture callers are checked with/without hardware gamma and with/without the
already gamma-corrected SSAA target. The core lifecycle suite additionally
compiles actual vk_read_pixels and checks raw-FBO gamma numerically. Existing
non-FBO behavior stays unchanged. An intentionally misaligned temporary allocator
forces a nonzero offset through actual RB_ReadPixels; a 641x479 patterned PNG
checks the actual encoder input pointer and every byte across row boundaries.

All seven tests pass on x64/x86 and x64 ASan. All seven fail against the saved
4dd25b0 production sources using DEBUG_SOURCE_DIR. Real GPU validation remains an
integration check; these fixtures do not claim exact HDR shader/dither fidelity.
