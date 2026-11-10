Native Vulkan backend imported from [JKSunny/EternalJK](https://github.com/JKSunny/EternalJK)
at commit `362b28150b83e1d077137c5ea67ebb8a123629ba` (2026-11-10 import).
Existing copyright and GPL notices are retained in the source files; see LICENSE.txt
at the repository root.
Whitespace is normalized on import; generated SPIR-V shader contents are unchanged.

This backend records Vulkan commands and uses Vulkan graphics pipelines, buffers,
images and SPIR-V shaders. Legacy `GL_*` material names describe the original game
shader format; they do not call OpenGL. No OpenGL library is linked by this target.

`BuildMPRdVulkan=ON` builds `rd-vulkan_<architecture>` with the engine's renderer
interface. Vulkan headers and generated SPIR-V are included; building the module
does not require the Vulkan SDK. Running it requires a Vulkan driver (MoltenVK on
macOS). GLSL templates and the upstream shader compiler sources/scripts are kept
under `shaders/` for shader development; regeneration requires the Vulkan SDK.
Upstream compiled helper executables are intentionally excluded.

Select it with `+set cl_renderer rd-vulkan`. Other renderers remain available for
compatibility and comparison. To package an external Vulkan module instead, set
`BuildMPRdVulkan=OFF` and `EXTERNAL_RD_VULKAN_MODULE` to its path.

EJK changes include its font/MP entity interface, authoritative shader hash
misses, bounded late-shader command updates with corrected sort-key shifts,
and persistent pipeline caching. `r_pipelineCache=1` saves compiled pipelines
under the writable game's `cache/vulkan/` directory at renderer teardown. The
file checks its length, checksum, Vulkan header, GPU/driver identity and process
architecture before passing the opaque payload to Vulkan. Invalid/incomplete
files are ignored; failure to create a seeded cache retries with an empty one.
The renderer cache uses `.dat` so pure servers can read it without treating it
as an unapproved game asset. `r_pipelineCache=0` disables disk reads/writes.

The engine fully tears down Vulkan resources before unloading this module,
including same-renderer video restarts. Its normal restart flag still preserves
Ghoul2 state and the engine-owned SDL window.
