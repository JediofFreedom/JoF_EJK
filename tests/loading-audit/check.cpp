#include <algorithm>
#include <atomic>
#include <cassert>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <setjmp.h>
#include <string>
#include <thread>
#include <vector>
#include <jpeglib.h>
#include <minizip/unzip.h>
#include <vulkan/vulkan_core.h>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)
#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))
#define PAD(v, a) (((v) + (a) - 1) & ~((a) - 1))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define Com_Memset std::memset
#define Com_Memcpy std::memcpy
#define S_COLOR_YELLOW ""
#define S_COLOR_RED ""
typedef unsigned char byte;
typedef int qhandle_t;
typedef int qboolean;
enum { qfalse, qtrue, ERR_DROP, TAG_TEMP_WORKSPACE };
const int MAX_QPATH = 64, MAX_COSMETIC_LENGTH = 32, MAX_EMOJI_LENGTH = 24, MAX_LOADABLE_EMOJIS = 256;
static void Q_strncpyz(char *out, const char *in, size_t length) { std::snprintf(out, length, "%s", in); }
static void Q_strlwr(char *s) { for (; *s; ++s) *s = char(std::tolower((unsigned char)*s)); }
static int Q_stricmp(const char *a, const char *b) {
    std::string x(a), y(b); Q_strlwr(&x[0]); Q_strlwr(&y[0]); return x.compare(y);
}
static void Com_sprintf(char *out, size_t length, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); std::vsnprintf(out, length, fmt, ap); va_end(ap);
}
static void Com_Printf(const char *, ...) {}
static void COM_StripExtension(const char *in, char *out, int length) {
    if (in != out) Q_strncpyz(out, in, length);
    char *dot = std::strrchr(out, '.'); if (dot) *dot = 0;
}
static std::atomic<int> allocations(0);
static void *Z_Malloc(size_t length, int, qboolean = qfalse) { ++allocations; return std::calloc(length ? length : 1, 1); }
static void Z_Free(void *p) { if (p) --allocations; std::free(p); }
extern "C" void *openjk_minizip_malloc(int n) { return Z_Malloc(n, 0); }
extern "C" int openjk_minizip_free(void *p) { Z_Free(p); return 0; }

static void checkArchive(const std::string &path) {
    std::printf("Archive: %s\n", path.c_str());
    unzFile original = unzOpen64(path.c_str()); CHECK(original);
    CHECK(!unzReOpen64(nullptr, original));
    CHECK(!unzReOpen64("nonexistent-audit-archive.pk3", original));
    CHECK(!unzReOpen64(path.c_str(), nullptr));
    CHECK(unzLocateFile(original, "first", 1) == UNZ_OK);
    const ZPOS64_T firstOffset = unzGetOffset64(original);
    CHECK(unzOpenCurrentFile(original) == UNZ_OK);
    byte prefix[73]; CHECK(unzReadCurrentFile(original, prefix, sizeof(prefix)) == sizeof(prefix));
    unzFile copy = unzReOpen64(path.c_str(), original); CHECK(copy);
    CHECK(unzLocateFile(copy, "second", 1) == UNZ_OK);
    CHECK(unzOpenCurrentFile(copy) == UNZ_OK);
    std::vector<byte> second(18 * 4096);
    CHECK(unzReadCurrentFile(copy, second.data(), int(second.size())) == int(second.size()));
    for (size_t i = 0; i < second.size(); ++i) CHECK(second[i] == "independent reader"[i % 18]);
    CHECK(unzCloseCurrentFile(copy) == UNZ_OK); // Includes CRC validation.
    std::vector<byte> first(256 * 1024 - sizeof(prefix));
    CHECK(unzReadCurrentFile(original, first.data(), int(first.size())) == int(first.size()));
    for (size_t i = 0; i < first.size(); ++i) CHECK(first[i] == byte(i + sizeof(prefix)));
    CHECK(unzCloseCurrentFile(original) == UNZ_OK);
    std::vector<std::thread> readers;
    for (int i = 0; i < 6; ++i) readers.emplace_back([&] {
        unzFile independent = unzReOpen64(path.c_str(), original); CHECK(independent);
        CHECK(unzSetOffset64(independent, firstOffset) == UNZ_OK);
        CHECK(unzOpenCurrentFile(independent) == UNZ_OK);
        std::vector<byte> bytes(256 * 1024);
        CHECK(unzReadCurrentFile(independent, bytes.data(), int(bytes.size())) == int(bytes.size()));
        for (size_t i = 0; i < bytes.size(); ++i) CHECK(bytes[i] == byte(i));
        CHECK(unzCloseCurrentFile(independent) == UNZ_OK); CHECK(unzClose(independent) == UNZ_OK);
    });
    for (auto &reader : readers) reader.join();
    CHECK(unzClose(original) == UNZ_OK);
    CHECK(unzSetOffset64(copy, firstOffset) == UNZ_OK);
    CHECK(unzOpenCurrentFile(copy) == UNZ_OK);
    CHECK(unzReadCurrentFile(copy, prefix, sizeof(prefix)) == sizeof(prefix));
    CHECK(unzClose(copy) == UNZ_OK); // Survives closing the mounted stream.
}

namespace modelTest {
enum { MOD_BAD, MOD_MD3 };
struct Model { int type; char name[MAX_QPATH]; };
static struct { int numModels; Model *models[8]; } tr;
struct Asset { int handle; char path[MAX_QPATH]; int hashNext; };
class CModelCacheManager {
public:
    static const int ASSET_HASH_SIZE = 1024;
    std::vector<Asset> assets;
    int assetHashHeads[ASSET_HASH_SIZE];
    CModelCacheManager() { std::fill(assetHashHeads, assetHashHeads + ASSET_HASH_SIZE, -1); }
    int GetModelHandle(const char *);
    void InsertModelHandle(const char *, int);
};
#include "model.h"
static void check() {
    Model first = {MOD_MD3, "Models/First.md3"}, second = {MOD_MD3, "models/second.md3"};
    tr.numModels = 3; tr.models[1] = &first; tr.models[2] = &second;
    CModelCacheManager cache;
    cache.InsertModelHandle("models/first.md3", 1);
    CHECK(cache.GetModelHandle("MODELS\\FIRST.MD3") == 1);
    tr.models[1] = &second;
    CHECK(cache.GetModelHandle("models/first.md3") == -1);
    tr.models[2] = &first;
    for (int i = 0; i < 10000; ++i) cache.InsertModelHandle("MODELS\\FIRST.MD3", 2);
    CHECK(cache.assets.size() == 1 && cache.GetModelHandle("models/first.md3") == 2);
    first.type = MOD_BAD; CHECK(cache.GetModelHandle("models/first.md3") == -1);
    tr.models[2] = nullptr; CHECK(cache.GetModelHandle("models/first.md3") == -1);
    tr.numModels = 2; CHECK(cache.GetModelHandle("models/first.md3") == -1);
}
}

namespace cosmeticTest {
#define COSMETIC_HATS_PATH "models/cosmetics/hats/"
#define COSMETIC_HATS_PATH_LENGTH strlen(COSMETIC_HATS_PATH)
#define COSMETIC_CAPES_PATH "models/cosmetics/capes/"
#define COSMETIC_CAPES_PATH_LENGTH strlen(COSMETIC_CAPES_PATH)
#define COSMETIC_HATS_LEGACY_PATH "models/players/hats/"
#define COSMETIC_CAPES_LEGACY_PATH "models/players/capes/"
#include "cosmetic_types.h"
static cosmetics_t localCosmetics;
static loadableEmoji_t emojis[MAX_LOADABLE_EMOJIS];
static std::map<std::string, std::vector<std::string>> files;
static std::map<std::string, int> models, shaders;
static std::vector<std::string> registrations;
static int now, redraws;
static struct { char infoScreenText[1024]; } cg;
static int fileList(const char *path, const char *, char *out, int length) {
    int used = 0, count = 0;
    for (auto &name : files[path]) {
        if (used + int(name.size()) + 1 > length) break;
        std::memcpy(out + used, name.c_str(), name.size() + 1); used += int(name.size()) + 1; ++count;
    }
    return count;
}
static int model(const char *path) { registrations.emplace_back(path); return models[path]; }
static int shader(const char *path) { registrations.emplace_back(path); return shaders[path]; }
static void error(int, const char *) { CHECK(false); }
static int milliseconds() { return now; }
static void redraw() { ++redraws; }
struct Trap {
    int (*FS_GetFileList)(const char *, const char *, char *, int);
    void (*Error)(int, const char *);
    int (*R_RegisterModel)(const char *);
    int (*R_RegisterShaderNoMip)(const char *);
    int (*Milliseconds)(); void (*UpdateScreen)();
} services = {fileList, error, model, shader, milliseconds, redraw}, *trap = &services;
static void Q_strstrip(char *, const char *, const char *);
#include "cosmetic.h"
static void check() {
    files[COSMETIC_HATS_PATH] = {"Custom.md3", "1invalid.md3", "crown.md3"};
    files[COSMETIC_CAPES_LEGACY_PATH] = {"vadercape.md3"};
    files["gfx/emoji"] = {"smile.png", "!D.png"};
    models[std::string(COSMETIC_HATS_PATH) + "Custom.md3"] = 7;
    models[std::string(COSMETIC_HATS_LEGACY_PATH) + "afro.md3"] = 9;
    shaders["gfx/emoji/!D.png"] = 11;
    CG_LoadAllCosmetics(); CG_LoadEmojis(); CHECK(registrations.empty());
    CHECK(localCosmetics.totalHats == 25 && localCosmetics.totalCapes == 8);
    CHECK(CG_CosmeticModel(localCosmetics.hats) == 7);
    CHECK(CG_CosmeticModel(localCosmetics.hats) == 7 && registrations.size() == 1);
    for (int i = 0; i < localCosmetics.totalHats; ++i) {
        auto *item = &localCosmetics.hats[i];
        if (!std::strcmp(item->name, "afro")) CHECK(CG_CosmeticModel(item) == 9);
        if (!std::strcmp(item->name, "bucket")) {
            CHECK(CG_CosmeticModel(item) == 0); size_t count = registrations.size();
            CHECK(CG_CosmeticModel(item) == 0 && registrations.size() == count);
        }
    }
    CHECK(CG_EmojiShader(1) == 11); size_t count = registrations.size();
    CHECK(CG_EmojiShader(1) == 11 && registrations.size() == count);
    CHECK(CG_EmojiShader(-1) == 0 && CG_EmojiShader(MAX_LOADABLE_EMOJIS) == 0);
    now = 0; CG_LoadingString("first"); CHECK(redraws == 1);
    now = 49; CG_LoadingString("next"); CHECK(redraws == 1 && !std::strcmp(cg.infoScreenText, "next"));
    now = 50; CG_LoadingString("third"); CHECK(redraws == 2);
    CG_LoadingString(""); CHECK(redraws == 3);
    CG_FreeCosmetics(); CHECK(!localCosmetics.hats && !localCosmetics.capes);
}
}

namespace jpegTest {
static std::vector<byte> input;
static long read(const char *, void **out) { *out = Z_Malloc(input.size(), 0); std::memcpy(*out, input.data(), input.size()); return long(input.size()); }
struct Services { long (*FS_ReadFile)(const char *, void **); void (*FS_FreeFile)(void *); void (*Z_Free)(void *); } ri = {read, Z_Free, Z_Free};
#include "jpeg.h"
static void check() {
    for (int gray = 0; gray < 2; ++gray) for (int progressive = 0; progressive < 2; ++progressive) {
        jpeg_compress_struct encoder = {}; jpeg_error_mgr err;
        encoder.err = jpeg_std_error(&err); jpeg_create_compress(&encoder);
        byte *encoded = nullptr; unsigned long length = 0; jpeg_mem_dest(&encoder, &encoded, &length);
        encoder.image_width = 17; encoder.image_height = 9; encoder.input_components = gray ? 1 : 3;
        encoder.in_color_space = gray ? JCS_GRAYSCALE : JCS_RGB; jpeg_set_defaults(&encoder);
        if (progressive) jpeg_simple_progression(&encoder);
        jpeg_start_compress(&encoder, TRUE);
        byte row[17 * 3]; for (size_t i = 0; i < sizeof(row); ++i) row[i] = byte(i * 13);
        while (encoder.next_scanline < encoder.image_height) { JSAMPROW ptr = row; jpeg_write_scanlines(&encoder, &ptr, 1); }
        jpeg_finish_compress(&encoder); jpeg_destroy_compress(&encoder);
        input.assign(encoded, encoded + length); std::free(encoded);
        byte *pixels = nullptr; int width, height; LoadJPG("generated.jpg", &pixels, &width, &height);
        CHECK(pixels && width == 17 && height == 9);
        jpeg_decompress_struct decoder = {}; decoder.err = jpeg_std_error(&err); jpeg_create_decompress(&decoder);
        jpeg_mem_src(&decoder, input.data(), input.size()); jpeg_read_header(&decoder, TRUE); decoder.out_color_space = JCS_RGB;
        jpeg_start_decompress(&decoder);
        for (int y = 0; y < height; ++y) {
            JSAMPROW ptr = row; jpeg_read_scanlines(&decoder, &ptr, 1);
            for (int x = 0; x < width; ++x) {
                CHECK(!std::memcmp(pixels + (y * width + x) * 4, row + x * 3, 3));
                CHECK(pixels[(y * width + x) * 4 + 3] == 255);
            }
        }
        jpeg_finish_decompress(&decoder); jpeg_destroy_decompress(&decoder); Z_Free(pixels);
        if (!progressive) {
            auto valid = input;
            for (size_t pos = 0; pos + 4 < input.size(); ++pos) {
                if (input[pos] != 0xff || input[pos + 1] != 0xda) continue;
                const size_t entropy = pos + 2 + input[pos + 2] * 256 + input[pos + 3];
                input.resize(entropy);
                input.insert(input.end(), {0xff, 0xd8, 0xff, 0xd9});
                LoadJPG("corrupt-scan.jpg", &pixels, &width, &height);
                CHECK(!pixels && width == 0 && height == 0);
                break;
            }
            input = valid;
        }
        input.resize(10); LoadJPG("truncated.jpg", &pixels, &width, &height);
        CHECK(!pixels && width == 0 && height == 0);
    }
    input = {'b', 'a', 'd'}; byte *pixels; int width, height;
    LoadJPG("bad.jpg", &pixels, &width, &height); CHECK(!pixels);
}
}

#include "fs_checks.h"

namespace uploadTest {
struct image_t { int internalFormat; VkImage handle; };
static struct { struct { VkDeviceSize size; byte *ptr; VkBuffer handle; } staging_buffer; } vk;
static struct { size_t uploadBytes; } vkLoadStats;
static struct Cvar { int integer; } setting = {1}, *r_imageUploadBatch = &setting;
static VkCommandBuffer image_upload_commands;
static VkDeviceSize image_upload_used;
static struct { int convert; } s_imageScratch;
static std::vector<byte> converted;
static byte *R_ImageScratchAlloc(int *, size_t size) { converted.resize(size); return converted.data(); }
struct Copy { VkImage image; VkDeviceSize offset; size_t size; };
static std::vector<Copy> pending;
static std::map<VkImage, std::vector<byte>> gpu;
static int submissions;
static void vk_flush_image_uploads();
static VkCommandBuffer vk_begin_command_buffer() { return reinterpret_cast<VkCommandBuffer>(uintptr_t(1)); }
static void vk_end_command_buffer(VkCommandBuffer, const char *) {
    vk_flush_image_uploads();
    for (auto &copy : pending) gpu[copy.image].assign(vk.staging_buffer.ptr + copy.offset, vk.staging_buffer.ptr + copy.offset + copy.size);
    pending.clear(); ++submissions;
}
static void vk_alloc_staging_buffer(VkDeviceSize size) {
    vk_flush_image_uploads(); std::free(vk.staging_buffer.ptr);
    vk.staging_buffer.size = size; vk.staging_buffer.ptr = static_cast<byte *>(std::malloc(size));
}
static int rygCompressedSize(int w, int h) { return ((w + 3) / 4) * ((h + 3) / 4) * 16; }
static void vk_record_image_layout_transition(VkCommandBuffer, VkImage, VkImageAspectFlags, VkImageLayout, VkImageLayout, uint32_t, uint32_t) {}
static void qvkCmdCopyBufferToImage(VkCommandBuffer, VkBuffer, VkImage image, VkImageLayout, int count, const VkBufferImageCopy *regions) {
    CHECK(regions[0].bufferOffset % 16 == 0);
    size_t bytes = 0;
    for (int i = 0; i < count; ++i) bytes += regions[i].imageExtent.width * regions[i].imageExtent.height * 4;
    CHECK(regions[0].bufferOffset + bytes <= vk.staging_buffer.size);
    pending.push_back({image, regions[0].bufferOffset, bytes});
}
#include "upload.h"
static VkImage handle(unsigned n) { return (VkImage)(uintptr_t)n; }
static void check() {
    byte data[20] = {1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255, 10, 11, 12, 255, 13, 14, 15, 255};
    int bpp;
    byte *out = vk_resample_image_data(VK_FORMAT_B8G8R8A8_UNORM, data, sizeof(data), &bpp);
    CHECK(converted.size() == 20 && bpp == 4 && out[0] == 3 && out[19] == 255);
    out = vk_resample_image_data(VK_FORMAT_R8G8B8_UNORM, data, sizeof(data), &bpp);
    CHECK(converted.size() == 15 && bpp == 3 && out[14] == 15);
    image_t a = {VK_FORMAT_R8G8B8A8_UNORM, handle(1)}, b = {VK_FORMAT_R8G8B8A8_UNORM, handle(2)};
    vk_upload_image_data(&a, 0, 0, 2, 2, 2, data, 20, qfalse);
    vk_upload_image_data(&b, 0, 0, 1, 1, 1, data + 4, 4, qfalse);
    CHECK(submissions == 0 && pending.size() == 2 && pending[1].offset == 32);
    vk_flush_image_uploads(); CHECK(submissions == 1 && gpu[a.handle] == std::vector<byte>(data, data + 20));
    CHECK(gpu[b.handle] == std::vector<byte>(data + 4, data + 8));
    // Force a capacity boundary; deferred data must survive reuse of offset zero.
    vk.staging_buffer.size = 32;
    vk_upload_image_data(&a, 0, 0, 2, 2, 2, data, 20, qfalse);
    // The allocator reserves 32 MiB in batch mode; use its actual final 16 bytes.
    image_upload_used = vk.staging_buffer.size - 16;
    vk_upload_image_data(&b, 0, 0, 2, 2, 2, data, 20, qfalse);
    CHECK(submissions == 2); vk_flush_image_uploads(); CHECK(submissions == 3);
    setting.integer = 0;
    vk_upload_image_data(&a, 0, 0, 1, 1, 1, data, 4, qtrue);
    CHECK(submissions == 4 && image_upload_used == 0 && pending.empty());
    vk_alloc_staging_buffer(64); CHECK(submissions == 4);
    std::free(vk.staging_buffer.ptr);
}
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    for (const char *name : {"ordinary.pk3", "zip64.pk3", "sfx-ordinary.pk3", "sfx-zip64.pk3"}) checkArchive(std::string(argv[1]) + "/" + name);
    unzFile empty = unzOpen64((std::string(argv[1]) + "/empty.pk3").c_str()); CHECK(empty);
    unzFile copy = unzReOpen64((std::string(argv[1]) + "/empty.pk3").c_str(), empty); CHECK(copy);
    unzClose(copy); unzClose(empty);
    modelTest::check(); cosmeticTest::check(); jpegTest::check(); uploadTest::check();
    filesystemTest::check(argv[1]);
    CHECK(allocations == 0); std::puts("Loading audit regressions passed.");
}
