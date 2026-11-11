# Vulkan target extent regressions

Compiles actual production attachment allocation calls, framebuffer creation,
refraction blit/copy, render-pass selection, viewport/scissor and attachment-clear
functions. Driver stubs record image dimensions supplied by production creation
calls and enforce framebuffer, render-area, transfer and clear bounds against
those dimensions. They do not substitute corrected transfer or clear sizes.
Allocation/bind failure and dedicated-memory behavior remain covered separately
by `vulkan-core-lifecycle` and `vulkan-resource-memory`.

The first 14 CTest groups cover ordinary controls, reduced bloom/blur/screenmap targets,
SSAA refraction, portal scissor transforms, entirely off-target views, explicit
glow-clear bounds and shared main-depth/glow-extraction pixel alignment. Each
group runs 961x541, 1024x768 and 320x241 sizes with MSAA on/off. Ordinary controls
and normal cases use SSAA off; remaining groups use SSAA on. A second build uses
the existing compile-time refraction-scale-1 branch, while the primary build
extracts the actual production scale definition.

Two additional presentation groups bring the suite to 16 cases. They compile
the actual surface-extent adjustment and framebuffer creation functions, plus
the actual postprocess viewport/scissor block. A 961x541 requested presentation
changing to 1024x768 must produce matching presentation framebuffers/pipelines,
preserve independent FBO render/capture resolutions (including SSAA), and update
direct-render depth/capture pixel grids. These cases include MSAA on/off.

Glow extraction must retain main-render dimensions because it LOADs and
depth-tests against the main pass depth image. Subsequent blur levels retain
their capture-based dimensions. Identity viewport/portal transforms use the
direct integer fast path; exact edge division is reserved for reduced targets.

`TARGET_SOURCE_DIR` can select an unmodified pre-fix snapshot containing
`vk_attachments.cpp`, `vk_frame.cpp`, `vk_init.cpp` and `vk_renderpass.cpp`.
The original 81b10be snapshot fails 12 of the original sizing groups and passes
both unchanged controls. Corrected sources pass all 16 cases on x64, x86 and x64
AddressSanitizer. The subsequent fullscreen regression is reproduced separately
against the original WSI function in `vulkan-core-lifecycle`; its new presentation
consistency cases use the actual corrected surface-extent helper.

The stubs do not execute shaders, rasterize geometry or model exact HDR pixels.
These tests establish source-level sizing and shared-depth coordinate contracts;
real GPU validation and visual checks remain separate integration evidence.
