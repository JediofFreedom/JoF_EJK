namespace hostCopyCapabilityTest {
static bool supported = true, optimal = true;
static unsigned queries;
static VkResult VKAPI_CALL query(VkPhysicalDevice, const VkPhysicalDeviceImageFormatInfo2 *request, VkImageFormatProperties2 *properties) {
    ++queries;
    CHECK(request->usage & VK_IMAGE_USAGE_HOST_TRANSFER_BIT);
    CHECK(request->usage & VK_IMAGE_USAGE_SAMPLED_BIT);
    CHECK(request->usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    if (!supported) return VK_ERROR_FORMAT_NOT_SUPPORTED;
    auto *performance = static_cast<VkHostImageCopyDevicePerformanceQuery *>(properties->pNext);
    CHECK(performance->sType == VK_STRUCTURE_TYPE_HOST_IMAGE_COPY_DEVICE_PERFORMANCE_QUERY);
    performance->optimalDeviceAccess = optimal;
    properties->imageFormatProperties.maxExtent = {4096, 4096, 1};
    properties->imageFormatProperties.maxMipLevels = 13;
    properties->imageFormatProperties.maxArrayLayers = 1;
    properties->imageFormatProperties.sampleCounts = VK_SAMPLE_COUNT_1_BIT;
    return VK_SUCCESS;
}
static void check() {
    VkImageCreateInfo image = {};
    image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO; image.imageType = VK_IMAGE_TYPE_2D;
    image.tiling = VK_IMAGE_TILING_OPTIMAL; image.format = VK_FORMAT_R8G8B8A8_UNORM;
    image.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image.extent = {256, 256, 1}; image.mipLevels = 9; image.arrayLayers = 1; image.samples = VK_SAMPLE_COUNT_1_BIT;
    CHECK(!VK_CanHostCopyImage(VK_NULL_HANDLE, image, nullptr) && queries == 0);
    CHECK(VK_CanHostCopyImage(VK_NULL_HANDLE, image, query));
    optimal = false; CHECK(!VK_CanHostCopyImage(VK_NULL_HANDLE, image, query)); optimal = true;
    supported = false; CHECK(!VK_CanHostCopyImage(VK_NULL_HANDLE, image, query)); supported = true;
    image.extent.width = 8192; CHECK(!VK_CanHostCopyImage(VK_NULL_HANDLE, image, query)); image.extent.width = 256;
    image.mipLevels = 14; CHECK(!VK_CanHostCopyImage(VK_NULL_HANDLE, image, query)); image.mipLevels = 9;
    image.samples = VK_SAMPLE_COUNT_4_BIT; CHECK(!VK_CanHostCopyImage(VK_NULL_HANDLE, image, query));
}
}

namespace rendererLoaderTest {
#define DEFAULT_RENDER_LIBRARY "rd-vulkan"
#define FALLBACK_RENDER_LIBRARY "rd-eternaljk"
#define ARCH_STRING "test"
#define DLL_EXT ".module"
const int MAX_OSPATH = 256, CVAR_ARCHIVE = 1, CVAR_LATCH = 2, CVAR_NONE = 0, ERR_FATAL = 1;
struct cvar_t { const char *string; int integer; } renderer, fallback, supported;
static cvar_t *cl_renderer = &renderer, *ui_vulkan_supported = &supported;
static std::string selected;
static std::vector<std::string> loads;
static bool vulkanPresent;
static cvar_t *Cvar_Get(const char *name, const char *value, int, const char *) {
    if (!std::strcmp(name, "cl_rendererFallback")) return &fallback;
    if (selected.empty()) selected = value;
    renderer.string = selected.c_str(); return &renderer;
}
static cvar_t *Cvar_Set2(const char *, const char *value, int, qboolean force) {
    CHECK(force); selected = value; renderer.string = selected.c_str(); return &renderer;
}
static void Com_Error(int, const char *, ...) { throw 1; }
static void *Sys_LoadDll(const char *name, qboolean) {
    loads.emplace_back(name);
    return vulkanPresent || std::strstr(name, "rd-eternaljk") ? reinterpret_cast<void *>(uintptr_t(1)) : nullptr;
}
#include "renderer_loader.h"
static void reset() { selected.clear(); loads.clear(); fallback.integer = 0; supported.integer = 1; vulkanPresent = true; }
static void check() {
    reset(); CHECK(CL_LoadRendererLibrary()); CHECK(loads == std::vector<std::string>{"rd-vulkan_test.module"});
    reset(); supported.integer = 0; bool failed = false;
    try { CL_LoadRendererLibrary(); } catch (...) { failed = true; }
    CHECK(failed && loads.empty());
    reset(); vulkanPresent = false; failed = false;
    try { CL_LoadRendererLibrary(); } catch (...) { failed = true; }
    CHECK(failed && loads.size() == 1 && selected == "rd-vulkan");
    reset(); vulkanPresent = false; fallback.integer = 1;
    CHECK(CL_LoadRendererLibrary() && loads.size() == 2 && selected == "rd-eternaljk");
    reset(); supported.integer = 0; fallback.integer = 1;
    CHECK(CL_LoadRendererLibrary() && loads == std::vector<std::string>{"rd-eternaljk_test.module"});
    reset(); selected = "rd-eternaljk"; CHECK(CL_LoadRendererLibrary() && loads.size() == 1);
}
#undef DEFAULT_RENDER_LIBRARY
#undef FALLBACK_RENDER_LIBRARY
#undef ARCH_STRING
#undef DLL_EXT
}

namespace lodTest {
const int MD3_MAX_LODS = 3, MD3_IDENT = 1, MDXA_IDENT = 2, MDXM_IDENT = 3;
enum { MOD_BAD, MOD_MESH, MOD_MDXA, MOD_MDXM, PRINT_WARNING };
struct model_t { int numLods = 0, index = 7, type = MOD_BAD; struct { void *mdv[3] = {}; } data; };
static std::map<std::string, unsigned> files;
static std::vector<std::string> requests;
struct Cache {
    qboolean LoadFile(const char *path, void **buffer, qboolean *cached) {
        requests.emplace_back(path); *cached = qfalse;
        auto entry = files.find(path); if (entry == files.end()) return qfalse;
        *buffer = &entry->second; return qtrue;
    }
} cache, *CModelCache = &cache;
static void print(int, const char *, ...) {}
static struct { void (*Printf)(int, const char *, ...); } ri = {print};
static unsigned LittleLong(unsigned n) { return n; }
static qboolean R_LoadMD3(model_t *m, int lod, void *, const char *) { m->type = MOD_MESH; m->data.mdv[lod] = reinterpret_cast<void *>(uintptr_t(lod + 1)); return qtrue; }
static qboolean R_LoadMDXA(model_t *m, void *, const char *, qboolean &) { m->type = MOD_MDXA; return qtrue; }
static qboolean R_LoadMDXM(model_t *m, void *, const char *, qboolean &) { m->type = MOD_MDXM; m->numLods = 2; return qtrue; }
#include "model_lods.h"
static void check() {
    files = {{"models/name.with.dots/item.md3", MD3_IDENT}, {"models/name.with.dots/item_2.md3", MD3_IDENT}};
    requests.clear(); model_t md3;
    CHECK(R_RegisterMD3("models/name.with.dots/item.md3", &md3) == 7);
    CHECK(md3.numLods == 2 && md3.data.mdv[0] && md3.data.mdv[1]);
    CHECK(requests[0] == "models/name.with.dots/item.md3" && requests[2] == "models/name.with.dots/item_2.md3");
    files = {{"player.glm", MDXM_IDENT}}; requests.clear(); model_t glm;
    CHECK(R_RegisterMD3("player.glm", &glm) == 7 && glm.numLods == 3 && requests.size() == 1);
    files = {{"anim.gla", MDXA_IDENT}}; requests.clear(); model_t gla;
    CHECK(R_RegisterMD3("anim.gla", &gla) == 7 && requests.size() == 1);
    files.clear(); requests.clear(); model_t missing; CHECK(R_RegisterMD3("missing.md3", &missing) == 0);
}
}

namespace sourceTest {
typedef void (*ImageLoaderFn)(const char *, byte **, int *, int *);
static void LoadJPG(const char *, byte **, int *, int *) {}
static void LoadPNG(const char *, byte **, int *, int *) {}
static void LoadTGA(const char *, byte **, int *, int *) {}
struct ImageLoaderMap { const char *extension; ImageLoaderFn loader; } imageLoaders[] = {{"jpg", LoadJPG}, {"png", LoadPNG}, {"tga", LoadTGA}};
static int numImageLoaders = 3;
static const ImageLoaderMap *FindImageLoader(const char *extension) {
    for (auto &loader : imageLoaders) if (!Q_stricmp(extension, loader.extension)) return &loader;
    return nullptr;
}
static std::map<std::string, fsReadFileIdentity_t> files;
static qboolean identify(const char *name, fsReadFileIdentity_t *identity, qboolean referenced) {
	auto found = files.find(name);
	if (found == files.end()) { *identity = {}; return qfalse; }
	*identity = found->second; return qtrue;
}
static struct { qboolean (*FS_GetReadFileIdentity)(const char *, fsReadFileIdentity_t *, qboolean); } ri = {identify};
#include "image_source.h"
static void check() {
	std::vector<byte> first, second;
	files = {{"image.jpg", {1, 2, 3, 4, 1, 0}}, {"image.png", {1, 2, 4, 7, 1, 0}}};
	CHECK(R_ImageSourceSignature("image.jpg", first, 1024));
	CHECK(R_ImageSourceSignature("image.jpg", second, 1024) && first == second);
	++files["image.png"].generation;
	CHECK(R_ImageSourceSignature("image.jpg", second, 1024) && first != second);
	files.erase("image.jpg"); CHECK(R_ImageSourceSignature("image.jpg", second, 1024) && first != second);
	files["image.png"].provider = 2;
	CHECK(!R_ImageSourceSignature("image.jpg", second, 1024) && second.empty()); // Loose override must decode anew.
	files.clear(); CHECK(!R_ImageSourceSignature("image.jpg", second, 1024));
	files["image.tga"] = {1, 2, 3, 4, 1, 0}; CHECK(!R_ImageSourceSignature("image", second, 2));
	CHECK(second.empty());
}

}

namespace preparationTest {
enum { IMGFLAG_MIPMAP = 1, IMGFLAG_PICMIP = 2, IMGFLAG_NOSCALE = 4,
       IMGFLAG_LIGHTMAP = 8, IMGFLAG_NO_COMPRESSION = 16, IMGFLAG_COLORSHIFT = 32, IMGFLAG_NOLIGHTSCALE = 64 };
const int IMAGE_UPLOAD_SCRATCH_MIN = 64, IMAGE_RESAMPLE_SCRATCH_MIN = 32;
typedef int imgFlags_t;
struct image_t { int width, height, flags, internalFormat; };
struct Image_Upload_Data { byte *buffer; int buffer_size, mip_levels, base_level_width, base_level_height; };
static struct Cvar { int integer; float value; } round = {}, nomip = {}, picmip = {}, color = {}, smart = {}, bits = {16}, simple = {1}, grey = {},
    *r_roundImagesDown = &round, *r_nomip = &nomip, *r_picmip = &picmip, *r_texturebits = &bits,
    *r_colorMipLevels = &color, *r_simpleMipMaps = &simple, *r_smartpicmip = &smart, *r_mapGreyScale = &grey;
static struct { bool mapLoading; } tr;
static struct { int maxTextureSize; bool deviceSupportsGamma; } glConfig = {4096};
static struct { int compressed_format; bool fboActive; } vk;
static byte s_gammatable[256], s_intensitytable[256];
static struct { uint64_t imagePrepareUs; } vkLoadStats;
static struct { int upload, resample, mip, compress; } s_imageScratch = {0, 1, 2, 3};
static std::vector<byte> scratch[4];
static thread_local bool privateWorker;
static byte *R_ImageScratchAlloc(int *slot, size_t bytes) { CHECK(!privateWorker); scratch[*slot].resize(bytes); return scratch[*slot].data(); }
static struct {
    static void *Hunk_AllocateTempMemory(int bytes) { CHECK(!privateWorker); return std::malloc(bytes); }
    static void Hunk_FreeTempMemory(void *memory) { CHECK(!privateWorker); std::free(memory); }
    static void Error(int, const char *, ...) { CHECK(false); }
} ri;
static void R_ColorShiftLightingBytes(byte *, byte *, qboolean) { CHECK(false); }
static bool vk_generate_image_upload_data(image_t *, byte *, Image_Upload_Data *, const TexturePreparationContext * = nullptr, TexturePreparationBuffers * = nullptr);
#define LERP(a,b,w) ((a)*(1.0f-(w))+(b)*(w))
#define LUMA(r,g,b) (0.2126f*(r)+0.7152f*(g)+0.0722f*(b))
#include "preparation.h"
static void check() {
    byte rgba[4 * 4 * 4];
    for (int i = 0; i < 16; ++i) { rgba[i * 4] = byte(i); rgba[i * 4 + 1] = 80; rgba[i * 4 + 2] = 170; rgba[i * 4 + 3] = 255; }
    for (int transparent = 0; transparent < 2; ++transparent) {
        rgba[3] = transparent ? 90 : 255;
        image_t image = {4, 4, IMGFLAG_MIPMAP | IMGFLAG_NOSCALE | IMGFLAG_NOLIGHTSCALE | IMGFLAG_NO_COMPRESSION, 0};
        Image_Upload_Data upload = {};
        vk_generate_image_upload_data(&image, rgba, &upload);
        CHECK(upload.buffer_size == (16 + 4 + 1) * 4 && upload.mip_levels == 3);
        CHECK(!std::memcmp(upload.buffer, rgba, sizeof(rgba)));
        CHECK(image.internalFormat == (transparent ? VK_FORMAT_B4G4R4A4_UNORM_PACK16 : VK_FORMAT_A1R5G5B5_UNORM_PACK16));
        int bpp;
        byte *converted = uploadTest::vk_resample_image_data(image.internalFormat, upload.buffer, upload.buffer_size, &bpp);
        CHECK(converted && bpp == 2 && uploadTest::converted.size() == (16 + 4 + 1) * 2);
    }
    // Deferred preparation must produce the bytes that immediate registration
    // would have produced, even after the world/UI phase and live cvars change.
    alignas(4) byte original[4 * 8 * 4];
    for (size_t i = 0; i < sizeof(original); ++i) original[i] = byte(i * 17 + 3);
    for (int world = 0; world < 2; ++world) for (int noMip = 0; noMip < 2; ++noMip)
    for (int level = 0; level < 2; ++level) for (int precision : {0,16})
    for (int filtering = 0; filtering < 2; ++filtering) for (int gamma = 0; gamma < 2; ++gamma) {
        tr.mapLoading = world != 0; nomip.integer = noMip; picmip.integer = level;
        bits.integer = precision; simple.integer = filtering; color.integer = round.integer = 0;
        grey.integer = 0; grey.value = 0.5f;
        glConfig.maxTextureSize = 4096; glConfig.deviceSupportsGamma = gamma != 0;
        for (int c = 0; c < 256; ++c) { s_gammatable[c] = byte(c * 3 / 4); s_intensitytable[c] = byte(std::min(255, c * 2)); }
        const TexturePreparationContext saved = R_CaptureTextureContext();
        const int flags = IMGFLAG_PICMIP | IMGFLAG_MIPMAP | IMGFLAG_NO_COMPRESSION;
        const std::string key = R_ResidentTextureKey("models/fixture.png", flags, saved);
        byte input[sizeof(original)]; std::memcpy(input, original, sizeof(input));
        R_GreyscaleFileImage(input, 4, 8, saved);
        image_t immediate = {4,8,flags,0}; Image_Upload_Data expectedUpload;
        vk_generate_image_upload_data(&immediate,input,&expectedUpload);
        const std::vector<byte> expected(expectedUpload.buffer, expectedUpload.buffer + expectedUpload.buffer_size);
        tr.mapLoading = !world; nomip.integer = !noMip; picmip.integer = 2;
        bits.integer = precision ? 0 : 16; simple.integer = !filtering; color.integer = 1;
        glConfig.maxTextureSize = 1; glConfig.deviceSupportsGamma = !gamma;
        grey.integer = 1; grey.value = 1;
        std::memset(s_gammatable, 0, sizeof(s_gammatable)); std::memset(s_intensitytable, 0, sizeof(s_intensitytable));
        CHECK(key == R_ResidentTextureKey("models/fixture.png", flags, saved));
        CHECK(key != R_ResidentTextureKey("models/fixture.png", flags, R_CaptureTextureContext()));
        std::memcpy(input, original, sizeof(input)); R_GreyscaleFileImage(input,4,8,saved);
        image_t deferred = {4,8,flags,0}; Image_Upload_Data actual;
        vk_generate_image_upload_data(&deferred,input,&actual,&saved);
        CHECK(immediate.internalFormat == deferred.internalFormat);
        CHECK(expectedUpload.base_level_width == actual.base_level_width && expectedUpload.base_level_height == actual.base_level_height);
        CHECK(expectedUpload.mip_levels == actual.mip_levels && expectedUpload.buffer_size == actual.buffer_size);
        CHECK(expected == std::vector<byte>(actual.buffer, actual.buffer + actual.buffer_size));
    }
    TexturePreparationContext map = {}; map.mapLoading = 1; map.mapGreyInteger = 1; map.mapGreyScale = 1;
    byte worldPixel[] = {20,100,230,77}; R_GreyscaleFileImage(worldPixel,1,1,map);
    CHECK(worldPixel[0] == worldPixel[1] && worldPixel[1] == worldPixel[2] && worldPixel[3] == 77);
    byte uiPixel[] = {20,100,230,77}; map.mapLoading = 0; R_GreyscaleFileImage(uiPixel,1,1,map);
    CHECK(uiPixel[0] == 20 && uiPixel[1] == 100 && uiPixel[2] == 230 && uiPixel[3] == 77);
    // Compare the actual shared CPU algorithms with private buffers under
    // concurrent HQ mip, grayscale, resampling, BC3 and both 16-bit modes.
    struct Fixture {
        int width, height, flags, format;
        TexturePreparationContext context;
        Image_Upload_Data description;
        std::vector<byte> source, expected;
    };
    std::vector<Fixture> fixtures;
    byte warmPixels[64] = {}, warmBlock[16];
    stb_compress_dxt_block(warmBlock,warmPixels,1,STB_DXT_NORMAL); // Same main-thread prewarm as production.
    for (const auto dimensions : {std::pair<int,int>(1,1),{1,32},{2,16},{16,8},{32,32}})
    for (int mode = 0; mode < 4; ++mode) for (int filtering = 0; filtering < 2; ++filtering)
    for (int level = 0; level < 2; ++level) for (float greyScale : {0.0f,0.35f,1.0f}) {
        Fixture fixture;
        fixture.width = dimensions.first; fixture.height = dimensions.second;
        fixture.flags = IMGFLAG_PICMIP | ((fixtures.size() & 1) ? IMGFLAG_MIPMAP : 0);
        const size_t pixels = size_t(fixture.width) * fixture.height;
        fixture.source.resize(pixels * 4);
        for (size_t i = 0; i < pixels; ++i) {
            fixture.source[i*4] = byte(i*13+5); fixture.source[i*4+1] = byte(i*17+17); fixture.source[i*4+2] = byte(i*23+29);
            fixture.source[i*4+3] = mode == 2 ? 127 : 255;
        }
        bits.integer = mode == 1 || mode == 2 ? 16 : 0; vk.compressed_format = mode == 3 ? VK_FORMAT_BC3_UNORM_BLOCK : 0;
        simple.integer = filtering; picmip.integer = level; nomip.integer = 0; color.integer = level;
        glConfig.maxTextureSize = 4096; glConfig.deviceSupportsGamma = false; tr.mapLoading = true;
        grey.integer = int(greyScale); grey.value = greyScale;
        for (int c = 0; c < 256; ++c) { s_gammatable[c] = byte(c*3/4); s_intensitytable[c] = byte(std::min(255,c*2)); }
        fixture.context = R_CaptureTextureContext();
        auto input = fixture.source; R_GreyscaleFileImage(input.data(),fixture.width,fixture.height,fixture.context);
        image_t image = {fixture.width,fixture.height,fixture.flags,0};
        CHECK(vk_generate_image_upload_data(&image,input.data(),&fixture.description,&fixture.context));
        fixture.format = image.internalFormat;
        fixture.expected.assign(fixture.description.buffer,fixture.description.buffer+fixture.description.buffer_size);
        fixture.description.buffer = nullptr;
        fixtures.push_back(std::move(fixture));
    }
    // Live engine state is deliberately incompatible with every captured job.
    std::memset(s_gammatable,0,sizeof(s_gammatable)); std::memset(s_intensitytable,0,sizeof(s_intensitytable));
    bits.integer = 123; simple.integer = 42; color.integer = 0; glConfig.maxTextureSize = 1;
    const uint64_t mainPreparationCounter = vkLoadStats.imagePrepareUs;
    std::vector<std::thread> threads;
    for (int thread = 0; thread < 4; ++thread) threads.emplace_back([&]() {
        privateWorker = true;
        for (const auto &fixture : fixtures) {
            auto input = fixture.source; R_GreyscaleFileImage(input.data(),fixture.width,fixture.height,fixture.context);
            const size_t budget = size_t(fixture.width)*fixture.height*16+65536;
            TexturePreparationBuffers storage(budget);
            image_t image = {fixture.width,fixture.height,fixture.flags,0}; Image_Upload_Data actual = {};
            CHECK(vk_generate_image_upload_data(&image,input.data(),&actual,&fixture.context,&storage));
            CHECK(storage.peak <= budget && storage.used <= budget);
            CHECK(image.internalFormat == fixture.format && actual.buffer_size == fixture.description.buffer_size && actual.mip_levels == fixture.description.mip_levels);
            CHECK(actual.base_level_width == fixture.description.base_level_width && actual.base_level_height == fixture.description.base_level_height);
            CHECK(!std::memcmp(actual.buffer,fixture.expected.data(),fixture.expected.size()));
            TexturePreparationBuffers exhausted(1);
            CHECK(!vk_generate_image_upload_data(&image,input.data(),&actual,&fixture.context,&exhausted));
        }
        privateWorker = false;
    });
    for (auto &thread : threads) thread.join();
    CHECK(vkLoadStats.imagePrepareUs == mainPreparationCounter);
    // Wide valid textures keep the same sampling formula beyond the old
    // 2048-index stack limit, including the former signed overflow at32768.
    for (int sourceWidth : {8192,32768}) {
        const int targetWidth = sourceWidth / 2;
        std::vector<byte> input(size_t(sourceWidth) * 2 * 4);
        for (int y = 0; y < 2; ++y) for (int x = 0; x < sourceWidth; ++x) {
            byte *pixel = input.data() + (size_t(y)*sourceWidth+x)*4;
            pixel[0] = byte(x); pixel[1] = byte(x >> 8); pixel[2] = byte(y*80); pixel[3] = 255;
        }
        TexturePreparationContext context = {};
        context.picmip = 1; context.maxTextureSize = 32768; context.mapLoading = 1; context.simpleMipMaps = 1;
        const int flags = IMGFLAG_PICMIP | IMGFLAG_NO_COMPRESSION | IMGFLAG_NOLIGHTSCALE;
        image_t image = {sourceWidth,2,flags,0}; Image_Upload_Data mainOutput;
        CHECK(vk_generate_image_upload_data(&image,input.data(),&mainOutput,&context));
        CHECK(mainOutput.base_level_width == targetWidth && mainOutput.base_level_height == 1);
        const std::vector<byte> expected(mainOutput.buffer,mainOutput.buffer+mainOutput.buffer_size);
        for (int x = 0; x < targetWidth; ++x) {
            CHECK(expected[x*4] == byte(x*2) && expected[x*4+1] == byte((x*2) >> 8));
            CHECK(expected[x*4+2] == 40 && expected[x*4+3] == 255);
        }
        privateWorker = true;
        TexturePreparationBuffers storage(size_t(sourceWidth)*2*16+65536);
        Image_Upload_Data workerOutput;
        CHECK(vk_generate_image_upload_data(&image,input.data(),&workerOutput,&context,&storage));
        CHECK(workerOutput.buffer_size == int(expected.size()) && !std::memcmp(workerOutput.buffer,expected.data(),expected.size()));
        const size_t minimum = size_t(targetWidth)*4*2 + size_t(targetWidth)*sizeof(unsigned)*2;
        TexturePreparationBuffers exhausted(minimum-1);
        CHECK(!vk_generate_image_upload_data(&image,input.data(),&workerOutput,&context,&exhausted));
        privateWorker = false;
    }
}
}

namespace registrationScopeTest {
typedef int imgFlags_t;
enum {IMGFLAG_LIGHTMAP = 1, IMGFLAG_COLORSHIFT = 2, IMGFLAG_TRANSFER_SRC = 4};
static bool textureRegistrationActive, textureBackendExecuting, pending;
static struct { unsigned frame_count; } vk;
static void R_FlushTextureJobs() { pending = false; }
#include "texture_scope.h"
static void check() {
    CHECK(!R_CanDeferTexture("ui/a.png",0));
    R_SetTextureRegistration(qtrue); CHECK(R_CanDeferTexture("ui/a.png",0));
    pending = true; R_TextureBackend(qtrue);
    CHECK(!pending && !R_CanDeferTexture("ui/a.png",0));
    R_TextureBackend(qfalse); CHECK(R_CanDeferTexture("ui/a.png",0));
    for (int flags : {IMGFLAG_LIGHTMAP,IMGFLAG_COLORSHIFT,IMGFLAG_TRANSFER_SRC}) CHECK(!R_CanDeferTexture("ui/a.png",flags));
    CHECK(!R_CanDeferTexture("*procedural",0));
    vk.frame_count = 1; CHECK(!R_CanDeferTexture("ui/a.png",0)); vk.frame_count = 0;
    pending = true; R_SetTextureRegistration(qfalse); CHECK(!pending && !R_CanDeferTexture("ui/a.png",0));
}
}

namespace modernTest {
static uint32_t loaderVersion;
static VkResult VKAPI_CALL enumerate(uint32_t *version) { *version = loaderVersion; return VK_SUCCESS; }
static VkResult VKAPI_CALL errorVersion(uint32_t *) { return VK_ERROR_INITIALIZATION_FAILED; }
struct Resource { static int count; Resource() { ++count; } ~Resource() { --count; } };
int Resource::count;
static void check() {
    CHECK(VK_SelectInstanceVersion(nullptr) == VK_API_VERSION_1_0);
    CHECK(VK_SelectInstanceVersion(errorVersion) == VK_API_VERSION_1_0);
    loaderVersion = VK_MAKE_API_VERSION(0, 1, 3, 240); CHECK(VK_SelectInstanceVersion(enumerate) == VK_API_VERSION_1_3);
    loaderVersion = VK_MAKE_API_VERSION(0, 1, 4, 368); CHECK(VK_SelectInstanceVersion(enumerate) == VK_API_VERSION_1_4);
    loaderVersion = VK_MAKE_API_VERSION(0, 1, 5, 1); CHECK(VK_SelectInstanceVersion(enumerate) == VK_API_VERSION_1_4);
    ResidentResourceCache<Resource> cache;
    AllocationRanges ranges(4096);
    size_t a, b, c;
    CHECK(ranges.allocate(512, 256, a) && a == 0);
    CHECK(ranges.allocate(128, 1024, b) && b == 1024);
    CHECK(ranges.allocate(256, 256, c) && c == 512);
    ranges.release(b, 128); ranges.release(a, 512); ranges.release(c, 256);
    CHECK(ranges.allocate(4096, 1, a) && a == 0); // Both neighbors and alignment gaps coalesce.
    CHECK(!ranges.allocate(1, 1, b)); ranges.release(a, 4096);
    for (unsigned trial = 1; trial < 30; ++trial) {
        AllocationRanges arena(65536);
        std::vector<std::pair<size_t, size_t>> used;
        unsigned random = trial;
        for (int i = 0; i < 100; ++i) {
            random = random * 1664525u + 1013904223u;
            const size_t bytes = 1 + (random % 1024), alignment = size_t(1) << ((random >> 16) % 11);
            size_t offset;
            if (!arena.allocate(bytes, alignment, offset)) continue;
            CHECK(offset % alignment == 0 && offset + bytes <= 65536);
            for (auto &span : used) CHECK(offset + bytes <= span.first || span.first + span.second <= offset);
            used.emplace_back(offset, bytes);
        }
        for (size_t i = 0; i < used.size(); i += 2) arena.release(used[i].first, used[i].second);
        for (size_t i = 1; i < used.size(); i += 2) arena.release(used[i].first, used[i].second);
        CHECK(arena.allocate(65536, 1, a) && a == 0);
    }
    MountedArchiveIdentities identities;
    MountedArchiveIdentities::Stamp stamp = {{1, 2, 3, 4, 5, 6, 7, 8}};
    const uint64_t original = identities.identify("archive.pk3", stamp);
    CHECK(original && identities.identify("archive.pk3", stamp) == original);
    ++stamp[4]; CHECK(identities.identify("archive.pk3", stamp) != original);
    --stamp[4]; CHECK(identities.identify("archive.pk3", stamp) != original); // A retired identity is never recycled.
    CHECK(identities.identify("other.pk3", stamp) != original);
    cache.setLimit(180);
    const std::vector<byte> bytes(10, 1);
    cache.store("a", bytes, std::make_shared<Resource>(), 50);
    cache.store("b", bytes, std::make_shared<Resource>(), 50);
    cache.store("c", bytes, std::make_shared<Resource>(), 50);
    auto pinned = cache.find("a", bytes); CHECK(pinned && Resource::count == 3);
    cache.store("d", bytes, std::make_shared<Resource>(), 50);
    CHECK(cache.bytes() == 180 && !cache.find("b", bytes)); // Reused UI assets survive one-off textures.
    auto changed = bytes; changed[0] = 2; CHECK(!cache.find("a", changed));
    cache.erase("a", pinned); CHECK(Resource::count == 3); // Current map still owns its image.
    cache.clear(); CHECK(Resource::count == 1);
    pinned.reset(); CHECK(Resource::count == 0);
    cache.store("active", bytes, std::make_shared<Resource>(), 50);
    pinned = cache.find("active", bytes); CHECK(!cache.evictIdle());
    pinned.reset(); CHECK(cache.evictIdle() && Resource::count == 0);
    cache.setLimit(0); cache.store("off", bytes, std::make_shared<Resource>(), 50); CHECK(cache.size() == 0 && Resource::count == 0);
    hostCopyCapabilityTest::check();
    rendererLoaderTest::check(); lodTest::check(); sourceTest::check(); preparationTest::check(); registrationScopeTest::check();
}
}
