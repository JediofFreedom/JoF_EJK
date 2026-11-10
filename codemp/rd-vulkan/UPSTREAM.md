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

The JKA upstream above was still its current head when checked on 2026-11-10.
The newer Quake3e history was audited through
`3266672f7d002ae54777e00a992c0f62bc0d3342`. Its swapchain first-acquire barrier,
depth-writing discard guard and cinematic texture-bind fixes were already
present in this JKA fork. Its model LOD registration fix
`36634a5883e2661fa8194c2545ea625f6785dc29` is adapted here for both MD3 and
JKA's GLM/GLA loaders, retaining embedded skeletal LODs.

Bundled official C and C++ Vulkan headers are updated to 1.4.368 from
[KhronosGroup/Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers)
at `88487f687f3100ebe1d3559a8a34d971ee85b977` (2026-11-06).
Their Apache/MIT licenses are included alongside them. Instance creation now
negotiates up to Vulkan 1.4 through vkEnumerateInstanceVersion, with a 1.0
loader fallback. The API version does not by itself add rendering features or
establish a performance gain.

Native Vulkan is the default when built/shipped. OpenGL fallback requires an
explicit `cl_rendererFallback 1`; the default is 0. The Vulkan module receives
no OpenGL procedure/extension callbacks and never creates an OpenGL context.
The complete client no longer links OpenGL on behalf of renderer modules.
Legacy renderers remain explicit alternatives for compatibility/comparison.

Renderer API 10 adds validated filesystem source identities for resident
textures. Engines/renderers must match; the API is checked before copying the
larger import table, so old combinations are rejected safely. Game/cgame/UI
and network protocol interfaces are unchanged.

`r_textureCacheMegs` bounds GPU texture residency across map changes, including
source identity metadata. The x64 default is 512 MiB (128 on x86), capped to a
quarter of device-local memory and a bounded allocation count. Zero disables
it. Retained images share independently owned GPU memory pages with aligned,
reusable ranges, separate from the map's image chunks;
descriptors are rebuilt after each map's pool reset. Image flags, pixel-processing
settings, color tables and resolved source identities must match. Missing/
changed candidates, pure restrictions and package replacement invalidate reuse.
Loose overrides decode normally. Reused assets still mark their actual package
as referenced. Dynamic updates invalidate the retained copy. Full renderer/
device shutdown releases the cache before unloading the DLL.

Archive identity is retained across an unchanged remount by comparing the OS
file ID, size, high-resolution modification/change times and validated central
directory checksum. Mounted archives remain immutable, as already required by
the engine's cached ZIP metadata and offsets. If a reliable identity cannot be
obtained, GPU reuse is disabled for that source.
