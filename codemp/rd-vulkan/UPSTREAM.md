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

Renderer API 11 includes validated filesystem source identities for resident
textures and the actual writable cache path (added after API 10).
Engines/renderers must match; the API is checked before copying the
larger import table, so old combinations are rejected safely. Game/cgame/UI
and network protocol interfaces are unchanged.

`r_textureCacheMegs` bounds GPU texture residency across map changes, including
source identity metadata. The default `-1` automatically selects one eighth of
device-local memory, up to 2 GiB on x64 (128 MiB on x86). Explicit values are
capped to a quarter of device-local memory and a bounded allocation count.
Available `VK_EXT_memory_budget` feedback further limits retained memory to
leave headroom for new allocations. Zero disables
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

## EJK renderer development

This is now an EJK-maintained derivative with its own synchronization,
transfer batching and pipeline lookup paths. Upstream remains credited;
importing a current backend does not establish that it is fully optimized.

Release jobs compile this source with the matching EJK engine. There is no
automatic Sunny source synchronization or Sunny binary download in the build
or updater; upstream changes are imported manually for review and validation.
The upstream links and copyright notices record this renderer's origins.
The separate `rend2-sync` workflow targets SomaZ/OpenJK's OpenGL Rend2 renderer
and does not update Vulkan.

Core Vulkan 1.3 synchronization2 is queried and enabled only when the instance,
physical device and feature bit support it. Image/buffer barriers use
vkCmdPipelineBarrier2 and queue submissions use vkQueueSubmit2. Devices without
that capability keep native Vulkan synchronization. `r_vulkanSynchronization2 0`
selects the old synchronization API for comparison. Unsupported submission
extension chains are preserved through the legacy submit function.

Static model index/vertex data now shares bounded, persistently mapped upload
storage with textures. It no longer allocates/maps/destroys staging memory
and submits/waits separately for each model buffer. A batch completes before
staging reuse, rendering, immediate operations or teardown. Dynamic buffer
updates retain their existing synchronous path. `r_imageUploadBatch 0` disables
batching for both images and static geometry. A matched T1_Fatal diagnostic
reduced queue submissions from 513 to 25 while uploading the same assets.

Pipeline definitions use a hash index over all semantic fields, ignoring
padding bytes. Base-index selection is preserved and the index rebuilds after
map truncation/reset. Gamma/capture, bloom and glow pipeline creation now uses
the driver's pipeline cache too.

Vulkan 1.4 host image copy is implemented for new textures using
vkTransitionImageLayout/vkCopyMemoryToImage, after querying device features,
supported layouts, format limits and optimalDeviceAccess. Dynamic updates
stay queued. Host-copy errors disable that path and fall back to staged data.
`r_hostImageCopy` defaults to 0: three alternating pairs on the RTX 5070 Ti
prototype measured 14.178 s staged versus 15.038 s with host copies for the
three-map cycle. The newer API was slower on this driver; requesting a newer
API is not a performance guarantee. That comparison preceded the shared
static-buffer batching change and does not compare against SJK.

Image memory chunks now respect memoryTypeBits when reused across formats and
usage flags. Khronos core and synchronization validation exposed splash-path
issues: missing transfer-source usage, discarded uploaded contents, an acquire
stage dependency and command-buffer reuse without a completion fence. These
are fixed, including flushing deferred uploads before the splash consumes them.

`tests/loading-audit/vulkan-smoke.ps1` exercises real rendering and can enable
an external Khronos validation layer without installing or changing driver
configuration. Unit tests use production feature selection, upload functions,
pipeline types/keys and synchronization helpers; they cover unsupported devices,
fallbacks, bounded buffer reuse, mip data, padding-independent keys and reset
behavior. The local Vulkan SDK was extracted only for validation; the renderer
still builds with its vendored headers and requires no SDK installation.

EJK's registration texture loader decodes JPG/PNG data using up to four CPU workers
(`r_textureWorkers -1`, or `0` for synchronous decoding). Original file reads,
package accounting, image registration and Vulkan operations stay on the main
thread. Pending input/results are bounded to 128 MiB on x64 and 32 MiB on x86;
rendering, registration completion and teardown drain or join the workers.
Model/UI registration is included after renderer initialization, while runtime
and backend-discovered images stay immediate. Each job captures processing
settings and color tables; forced cvar changes cannot alter its pixels or key.
`r_textureWorkerPrepare 1` also moves greyscale, resampling, mip generation and
compression onto those workers using private bounded scratch. `0` retains
parallel decoding with main-thread preparation. Conservative reservations cover
admitted encoded/decoded/prepared/scratch buffers; the next source read and
codec internals are outside that pending-queue limit. Oversized jobs drain and
fall back to the ordinary path, preserving texture quality.
Shared JPG/PNG/TGA decoders validate file bounds and output sizes without
worker access to the engine allocator or console. PNG color transforms retain
the existing byte values without adding a gamma conversion.

`r_textureDiskCacheMegs` enables the experimental persistent prepared-mip cache;
it defaults to 0 pending measurements. It uses actual source bytes and all
processing settings, not process-local archive identities, to invalidate data.
Headers, complete mip sizes, formats and payload checksums are validated before
upload. The bounded asynchronous writer publishes complete entries atomically;
cache failures fall back to ordinary decoding. Prepared 16-bit entries store
RGBA8 CPU data so the existing upload conversion still runs once.

Stable content keys and corruption checks use the unmodified BSD-2-Clause
[xxHash v0.8.3](../../lib/xxhash/UPSTREAM.md). Its license must accompany binary
distributions. Source provenance for Sunny/Quake3e remains as documented above.
