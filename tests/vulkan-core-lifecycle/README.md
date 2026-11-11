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
remain unchanged at this layer. MSAA requests include non-power-of-two3/5 values.
