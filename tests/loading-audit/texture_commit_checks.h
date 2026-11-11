#include <condition_variable>
#include <deque>
#include <memory>

namespace textureCommitTest {
struct image_t { char imgName[64]; int width, height; };
struct TextureDecodeJob {
    image_t *image;
    EncodedImage encoded = {};
    DecodedImage decoded = {};
    byte *fallbackPixels = nullptr;
    struct { std::vector<byte> bytes; } prepared;
    std::vector<byte> source;
    std::string cacheKey;
    size_t bytes = 0;
    std::mutex mutex;
    std::condition_variable complete;
    bool done = true, diskCache = true;
    char error[256] = {};
    int decodeMs = 1;
    ~TextureDecodeJob() { std::free(decoded.pixels); }
};
static std::deque<std::shared_ptr<TextureDecodeJob>> textureJobs;
static size_t textureJobBytes;
static int textureWorkerWaitMs;
static struct { int imageDecodeMs; } vkLoadStats;
static struct {
    static int Milliseconds() { return 0; }
    static void FS_FreeFile(void *data) { ::Z_Free(data); }
    static void Z_Free(void *data) { ::Z_Free(data); }
    static void Printf(int, const char *, ...) {}
} ri;
static const int PRINT_WARNING = 1;
static std::vector<byte> fallback;
static int fallbackReads, uploadedWidth, uploadedHeight;
static bool uploadedDiskCache, uploadedSource;
static byte firstPixel[4];
static void *zoneAllocate(size_t size) { return Z_Malloc(size, 0); }
static void R_LoadImage(const char *, byte **pixels, int *width, int *height, char *resolved) {
    ++fallbackReads;
    const ImageAllocator allocator = {zoneAllocate, Z_Free};
    DecodedImage image;
    CHECK(pngTest::R_DecodePNG(fallback.data(), fallback.size(), allocator, image, nullptr, 0));
    *pixels = image.pixels; *width = image.width; *height = image.height;
    Q_strncpyz(resolved, "texture.png", MAX_QPATH);
}
static void R_FinishFileImage(image_t *image, byte *pixels, std::vector<byte> source,
                             const std::string &, const char *, TextureDecodeJob *job) {
    uploadedWidth = image->width; uploadedHeight = image->height;
    uploadedDiskCache = job->diskCache; uploadedSource = !source.empty();
    if (pixels) std::memcpy(firstPixel, pixels, sizeof(firstPixel));
}
#include "texture_commit.h"

static std::vector<byte> jpeg(int size) {
    std::vector<byte> pixels(size_t(size) * size * 3, 67), encoded(pixels.size() + 65536);
    const size_t length = jpegTest::RE_SaveJPGToBuffer(encoded.data(), encoded.size(), 90, size, size, pixels.data(), 0);
    CHECK(length); encoded.resize(length); return encoded;
}
static void pngFallback(int width, int height) {
    std::vector<byte> pixels(size_t(width) * height * 3, 67);
    CHECK(pngTest::RE_SavePNG("memory", pixels.data(), width, height, 3) == 0);
    fallback = pngTest::encoded;
}
static std::shared_ptr<TextureDecodeJob> queue(const std::vector<byte> &encoded, image_t &image) {
    CHECK(textureJobs.empty() && textureJobBytes == 0);
    auto job = std::make_shared<TextureDecodeJob>();
    job->encoded.data = Z_Malloc(encoded.size(), 0);
    std::memcpy(job->encoded.data, encoded.data(), encoded.size()); job->encoded.length = encoded.size();
    CHECK(R_ProbeEncodedImage(encoded.data(), encoded.size(), EncodedImageFormat::JPG, job->encoded.width, job->encoded.height));
    job->encoded.format = EncodedImageFormat::JPG;
    Q_strncpyz(job->encoded.resolvedName, "texture.jpg", MAX_QPATH);
    image = {}; Q_strncpyz(image.imgName, "texture", sizeof(image.imgName));
    image.width = job->encoded.width; image.height = job->encoded.height;
    job->image = &image; job->bytes = encoded.size(); job->source = {1,2,3};
    textureJobBytes = job->bytes; textureJobs.push_back(job);
    fallbackReads = 0; uploadedWidth = uploadedHeight = 0; uploadedDiskCache = uploadedSource = false;
    std::memset(firstPixel, 0, sizeof(firstPixel));
    return job;
}
static void check() {
    auto primary = jpeg(256);
    // Keep its real 256x256 header, then inject an invalid marker into the scan.
    // The probe succeeds, but the actual memory decoder rejects the pixels.
    bool corrupted = false;
    for (size_t pos = 0; pos + 4 < primary.size(); ++pos) {
        if (primary[pos] != 255 || primary[pos + 1] != 218) continue;
        const size_t entropy = pos + 2 + size_t(primary[pos + 2]) * 256 + primary[pos + 3];
        primary.resize(entropy); primary.insert(primary.end(), {255,216,255,217}); corrupted = true; break;
    }
    CHECK(corrupted);
    const ImageAllocator allocator = {std::malloc, std::free};
    image_t image;
    pngFallback(128, 128);
    {
        auto job = queue(primary, image);
        CHECK(!jpegTest::R_DecodeJPG(primary.data(), primary.size(), allocator, job->decoded, job->error, sizeof(job->error)));
        R_CommitTextureJob();
        CHECK(fallbackReads == 1 && uploadedWidth == 128 && uploadedHeight == 128);
        CHECK(firstPixel[0] == 67 && firstPixel[1] == 67 && firstPixel[2] == 67 && firstPixel[3] == 255);
        CHECK(!uploadedDiskCache && !uploadedSource && !job->fallbackPixels && !job->encoded.data);
        CHECK(textureJobs.empty() && !textureJobBytes);
    }
    // A primary decoder result must still agree with the original probe.
    {
        auto job = queue(primary, image);
        const auto other = jpeg(128);
        CHECK(jpegTest::R_DecodeJPG(other.data(), other.size(), allocator, job->decoded, nullptr, 0));
        R_CommitTextureJob();
        CHECK(fallbackReads == 0 && uploadedWidth == 2 && uploadedHeight == 2);
        CHECK(firstPixel[0] == 255 && firstPixel[1] == 0 && firstPixel[2] == 255);
        CHECK(!uploadedDiskCache && !uploadedSource && textureJobs.empty() && !textureJobBytes);
    }
    // Alternate images still obey the renderer's power-of-two restriction.
    pngFallback(127, 128);
    {
        auto job = queue(primary, image);
        R_CommitTextureJob();
        CHECK(fallbackReads == 1 && uploadedWidth == 2 && uploadedHeight == 2);
        CHECK(!job->fallbackPixels && !job->encoded.data && textureJobs.empty() && !textureJobBytes);
    }
}
}
