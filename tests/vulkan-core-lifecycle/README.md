# Vulkan core lifecycle regressions

Compiles the actual frame begin/end/present/readback and backend command dispatch
functions with a mock Vulkan driver, plus the actual MSAA selection block. Injects
success, suboptimal, timeout, not-ready, out-of-date, device loss, and minimize/
restore changes between acquisition, submission and presentation. Asserts no
unacquired-image accesses/semaphore waits, pending-buffer resets, or skipped
command-size/state corruption. Tests readback before submission, released direct
images, bounds and transfer-to-host visibility, and sparse/stencil sample support.

Use `CORE_SOURCE_DIR` for a saved pre-fix source snapshot (the fixture automatically adapts the legacy void
`vk_begin_frame` return convention without changing its algorithm).
Driver validation and real window transitions remain separate integration checks.

The fixture uses the actual logging-only VK_CHECK helper, actual log2pad
(round-up and round-down behavior), actual backend DrawBuffer and command dispatch,
and actual gamma-table/readback functions. Explicit error branches, rather than
a stronger mock checker, must stop failed command-buffer/fence operations.

The raw-FBO capture case checks gamma once; the SSAA target and non-FBO bytes
remain unchanged at this layer. MSAA requests include non-power-of-two 3/5 values.

The postprocessing cases compile the actual bloom/glow, pipeline-bind and geometry
functions. The mock driver invalidates dynamic scissor and viewport on a static
pipeline bind, then checks the next indexed draw. Identical rectangles must be
set again after bloom/glow; changed rectangles and the dynamic-only draw cache
remain valid. The latter asserts one pipeline bind, viewport and scissor update
for 100 draws. Production descriptor constants and dynamic-state enums are
extracted rather than duplicated. `DYNAMIC_SOURCE_DIR` can select pre-fix bloom,
glow and geometry sources independently of `CORE_SOURCE_DIR`.

Descriptor restore cases additionally run the actual descriptor-update and bind
helpers. The mock models Vulkan pipeline-layout compatibility and descriptor
set disturbance: a fogged world draw, incompatible postFX descriptors, and a
world draw requesting the same cached fog image must rebind fog set 4. Bloom
and glow are both checked; a UI draw with a null unused fog set must stay valid.
