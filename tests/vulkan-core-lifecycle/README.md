# Vulkan core lifecycle regressions

Compiles the actual frame begin/end/present/readback and backend command dispatch
functions with a mock Vulkan driver, plus the actual MSAA selection block. Injects
success, suboptimal, timeout, not-ready, out-of-date, device loss, and minimize/
restore changes between acquisition, submission and presentation. Asserts no
unacquired-image accesses/semaphore waits, pending-buffer resets, or skipped
command-size/state corruption. Tests readback before submission, released direct
images, bounds and transfer-to-host visibility, and sparse/stencil sample support.

Use `CORE_SOURCE_DIR` for a saved pre-fix source snapshot (a legacy void
`vk_begin_frame` snapshot needs the documented compatibility return adapter).
Driver validation and real window transitions remain separate integration checks.
