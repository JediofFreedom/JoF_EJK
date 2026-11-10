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
struct image_t { int width, height, flags, internalFormat; };
struct Image_Upload_Data { byte *buffer; int buffer_size, mip_levels, base_level_width, base_level_height; };
static struct Cvar { int integer; } off = {0}, bits = {16}, *r_roundImagesDown = &off, *r_nomip = &off,
    *r_picmip = &off, *r_texturebits = &bits, *r_colorMipLevels = &off;
static struct { bool mapLoading; } tr;
static struct { int maxTextureSize; } glConfig = {4096};
static struct { int compressed_format; } vk;
static struct { int upload, resample, mip, compress; } s_imageScratch = {0, 1, 2, 3};
static std::vector<byte> scratch[4];
static byte *R_ImageScratchAlloc(int *slot, size_t bytes) { scratch[*slot].resize(bytes); return scratch[*slot].data(); }
static void ResampleTexture(unsigned *, int, int, unsigned *, int, int) { CHECK(false); }
static void R_ColorShiftLightingBytes(byte *, byte *, qboolean) { CHECK(false); }
static void R_LightScaleTexture(byte *, int, int, qboolean) { CHECK(false); }
static void R_MipMap(byte *in, byte *out, int width, int height) {
    const size_t bytes = size_t(std::max(1, width / 2)) * std::max(1, height / 2) * 4;
    std::memmove(out, in, bytes);
}
static void R_BlendOverTexture(byte *, int, int) { CHECK(false); }
static int rygCompressedSize(int w, int h) { return ((w + 3) / 4) * ((h + 3) / 4) * 16; }
static void rygCompress(byte *, byte *, int, int) { CHECK(false); }
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
    rendererLoaderTest::check(); lodTest::check(); sourceTest::check(); preparationTest::check();
}
}
