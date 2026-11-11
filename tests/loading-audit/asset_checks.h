using jpegTest::ImageAllocator;
using jpegTest::DecodedImage;
using jpegTest::EncodedImage;
using jpegTest::EncodedImageFormat;
using jpegTest::R_ProbeEncodedImage;
namespace encodedReadTest {
static volatile int loaderKind;
static void LoadJPG() { loaderKind = 1; }
static void LoadPNG() { loaderKind = 2; }
static void LoadTGA() { loaderKind = 3; }
struct ImageLoaderMap { const char *extension; void (*loader)(); };
static ImageLoaderMap imageLoaders[] = {{"jpg", LoadJPG}, {"png", LoadPNG}, {"tga", LoadTGA}};
static const int numImageLoaders = 3;
static std::map<std::string, std::vector<byte>> files;
static std::vector<std::string> reads;
static const ImageLoaderMap *FindImageLoader(const char *extension) {
    for (const auto &loader : imageLoaders) if (!Q_stricmp(extension, loader.extension)) return &loader;
    return nullptr;
}
static struct {
    static long FS_ReadFile(const char *name, void **data) {
        reads.push_back(name); auto entry = files.find(name);
        if (entry == files.end()) { *data = nullptr; return -1; }
        *data = Z_Malloc(entry->second.size(), 0);
        std::memcpy(*data, entry->second.data(), entry->second.size()); return long(entry->second.size());
    }
    static void FS_FreeFile(void *data) { Z_Free(data); }
} ri;
#include "encoded_reader.h"
}
namespace pngTest {
typedef int fileHandle_t;
static std::vector<byte> encoded;
static bool failWrite;
static struct {
    static int FS_FOpenFileWrite(const char *, qboolean) { encoded.clear(); return 1; }
    static int FS_Write(const void *data, int length, int) {
        if (failWrite) return 0;
        const byte *p = static_cast<const byte *>(data);
        encoded.insert(encoded.end(), p, p + length); return length;
    }
    static void FS_FCloseFile(int) {}
} ri;
#include "png_functions.h"
}
namespace waveTest {
#include "wav_type.h"
#include "wav.h"
static void u32(std::vector<byte> &out, unsigned v) {
    for (unsigned i = 0; i < 4; ++i) out.push_back(byte(v >> (8 * i)));
}
static void chunk(std::vector<byte> &out, const char *id, const std::vector<byte> &data) {
    out.insert(out.end(), id, id + 4); u32(out, unsigned(data.size()));
    out.insert(out.end(), data.begin(), data.end()); if (data.size() & 1) out.push_back(0);
}
static void check() {
    for (bool dataFirst : {false, true}) for (byte bits : {byte(8), byte(16)}) {
        std::vector<byte> wav = {'R','I','F','F',0,0,0,0,'W','A','V','E'};
        std::vector<byte> fmt = {1,0,1,0,0x22,0x56,0,0,0x44,0xac,0,0,2,0,bits,0};
        const std::vector<byte> sound(128, 0x33);
        chunk(wav, "JUNK", {1,2,3});
        if (dataFirst) chunk(wav, "data", sound);
        const size_t formatOffset = wav.size() + 8;
        chunk(wav, "fmt ", fmt);
        if (!dataFirst) chunk(wav, "data", sound);
        const unsigned size = unsigned(wav.size() - 8);
        for (unsigned i = 0; i < 4; ++i) wav[4 + i] = byte(size >> (8 * i));
        const auto valid = GetWavinfo("test.wav", wav.data(), int(wav.size()));
        CHECK(valid.channels == 1 && valid.width == bits / 8 && valid.rate == 22050);
        CHECK(valid.samples == 128 / valid.width && wav[valid.dataofs] == 0x33);
        for (size_t cut = 0; cut < wav.size(); ++cut)
            CHECK(!GetWavinfo("truncated.wav", wav.data(), int(cut)).samples);
        for (byte badBits : {byte(0), byte(7), byte(24), byte(32)}) {
            wav[formatOffset + 14] = badBits;
            CHECK(!GetWavinfo("bad-width.wav", wav.data(), int(wav.size())).channels);
        }
        wav[formatOffset + 14] = bits;
        std::memset(wav.data() + formatOffset + 4, 0, 4);
        CHECK(!GetWavinfo("zero-rate.wav", wav.data(), int(wav.size())).channels);
    }
}
}
namespace assetTest {
#include "tga.h"
static std::vector<byte> read(const std::string &path) {
    std::ifstream file(path, std::ios::binary);
    CHECK(file.good()); return std::vector<byte>(std::istreambuf_iterator<char>(file), {});
}
static void check(const char *directory) {
    const ImageAllocator allocator = {std::malloc, std::free};
    for (unsigned bits : {8u, 24u, 32u}) for (unsigned direction : {0u, 16u, 32u, 48u}) for (bool rle : {false, true}) {
        if (rle && bits == 8) continue;
        std::vector<byte> tga(18, 0);
        tga[0] = 3; tga[2] = byte(rle ? 10 : bits == 8 ? 3 : 2);
        tga[12] = tga[14] = 2; tga[16] = byte(bits); tga[17] = byte(direction);
        tga.insert(tga.end(), {1,2,3});
        if (rle) tga.push_back(3); // One raw packet crossing the row boundary.
        std::vector<byte> expected(16);
        for (unsigned i = 0; i < 4; ++i) {
            const byte value = byte(i * 10 + 20);
            const unsigned x = direction & 16 ? 1 - i % 2 : i % 2;
            const unsigned y = direction & 32 ? i / 2 : 1 - i / 2;
            for (unsigned c = 0; c < bits / 8; ++c) tga.push_back(value + c);
            byte *out = expected.data() + (y * 2 + x) * 4;
            out[0] = bits == 8 ? value : value + 2;
            out[1] = bits == 8 ? value : value + 1; out[2] = value; out[3] = bits == 32 ? value + 3 : 255;
        }
        DecodedImage result;
        CHECK(assetTest::R_DecodeTGA(tga.data(), tga.size(), allocator, result, nullptr, 0));
        CHECK(result.width == 2 && result.height == 2 && !std::memcmp(result.pixels, expected.data(), 16));
        std::free(result.pixels);
        for (size_t cut = 0; cut < tga.size(); ++cut) {
            CHECK(!assetTest::R_DecodeTGA(tga.data(), cut, allocator, result, nullptr, 0)); CHECK(!result.pixels);
        }
        if (rle) {
            tga.resize(21); tga.push_back(131); // Repeated pixel packet crossing rows.
            for (unsigned c = 0; c < bits / 8; ++c) tga.push_back(42);
            CHECK(assetTest::R_DecodeTGA(tga.data(), tga.size(), allocator, result, nullptr, 0));
            for (int i = 0; i < 4; ++i) CHECK(result.pixels[i * 4] == 42);
            std::free(result.pixels);
            tga[21] = 132; // Five pixels in a four-pixel image used to overrun output.
            CHECK(!assetTest::R_DecodeTGA(tga.data(), tga.size(), allocator, result, nullptr, 0));
        }
    }
    for (const char *name : {"rgb", "rgba", "rgb16", "rgba16", "palette", "gray", "gray-alpha", "gray-low"}) {
        const auto source = read(std::string(directory) + "/" + name + ".png");
        const auto expected = read(std::string(directory) + "/" + name + ".rgba");
        DecodedImage image; char error[256];
        CHECK(pngTest::R_DecodePNG(source.data(), source.size(), allocator, image, error, sizeof(error)));
        CHECK(image.width == 2 && image.height == 2);
        CHECK(!std::memcmp(image.pixels, expected.data(), expected.size())); std::free(image.pixels);
        // Every possible truncation used to reach an unbounded memcpy callback.
        for (size_t cut = 0; cut < source.size(); ++cut) {
            CHECK(!pngTest::R_DecodePNG(source.data(), cut, allocator, image, error, sizeof(error)));
            CHECK(!image.pixels && !image.width && !image.height);
        }
        std::vector<std::thread> threads;
        for (int n = 0; n < 4; ++n) threads.emplace_back([&]() {
            for (int i = 0; i < 25; ++i) {
                DecodedImage decoded;
                CHECK(pngTest::R_DecodePNG(source.data(), source.size(), allocator, decoded, nullptr, 0));
                CHECK(!std::memcmp(decoded.pixels, expected.data(), expected.size())); std::free(decoded.pixels);
            }
        });
        for (auto &thread : threads) thread.join();
    }
    for (int channels : {3, 4}) {
        std::vector<byte> input(2 * 2 * channels);
        for (size_t i = 0; i < input.size(); ++i) input[i] = byte(i * 13);
        CHECK(pngTest::RE_SavePNG("memory", input.data(), 2, 2, channels) == 0);
        DecodedImage decoded;
        CHECK(pngTest::R_DecodePNG(pngTest::encoded.data(), pngTest::encoded.size(), allocator, decoded, nullptr, 0));
        for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) {
            CHECK(!std::memcmp(decoded.pixels + (y * 2 + x) * 4, input.data() + ((1 - y) * 2 + x) * channels, channels));
            if (channels == 3) CHECK(decoded.pixels[(y * 2 + x) * 4 + 3] == 255);
        }
        std::free(decoded.pixels);
    }
    pngTest::failWrite = true;
    byte rgb[12] = {};
    CHECK(pngTest::RE_SavePNG("full-disk", rgb, 2, 2, 3) == -1);
    pngTest::failWrite = false;
    CHECK(pngTest::RE_SavePNG("bad", rgb, 0, 2, 3) == -1);
    std::vector<byte> jpeg(65536);
    CHECK(jpegTest::RE_SaveJPGToBuffer(jpeg.data(), 1, 100, 2, 2, rgb, 0) == 0);
    const size_t count = jpegTest::RE_SaveJPGToBuffer(jpeg.data(), jpeg.size(), 100, 2, 2, rgb, 0);
    CHECK(count > 0);
    DecodedImage decoded;
    CHECK(jpegTest::R_DecodeJPG(jpeg.data(), count, allocator, decoded, nullptr, 0));
    CHECK(decoded.width == 2 && decoded.height == 2); std::free(decoded.pixels);
    int width, height;
    CHECK(R_ProbeEncodedImage(jpeg.data(), count, EncodedImageFormat::JPG, width, height));
    CHECK(width == 2 && height == 2);
    for (size_t cut = 0; cut < count; ++cut)
        R_ProbeEncodedImage(jpeg.data(), cut, EncodedImageFormat::JPG, width, height);
    CHECK(jpegTest::RE_SaveJPGToBuffer(jpeg.data(), jpeg.size(), 90, 0, 2, rgb, 0) == 0);
    CHECK(jpegTest::RE_SaveJPGToBuffer(jpeg.data(), jpeg.size(), 90, 2, 2, rgb, -1) == 0);
    encodedReadTest::files["texture.jpg"] = std::vector<byte>(jpeg.begin(), jpeg.begin() + count);
    encodedReadTest::files["texture.png"] = read(std::string(directory) + "/rgb.png");
    EncodedImage encoded;
    CHECK(encodedReadTest::R_ReadEncodedImage("texture.PNG", encoded));
    // The test filesystem is case sensitive, so the requested uppercase path
    // misses and ordinary priority chooses JPEG before PNG.
    CHECK(encoded.format == EncodedImageFormat::JPG && encoded.width == 2);
    Z_Free(encoded.data);
    encodedReadTest::reads.clear();
    CHECK(encodedReadTest::R_ReadEncodedImage("texture.png", encoded));
    CHECK(encoded.format == EncodedImageFormat::PNG && encodedReadTest::reads.size() == 1);
    Z_Free(encoded.data);
    encodedReadTest::files["texture.jpg"] = {'b','a','d'};
    CHECK(!encodedReadTest::R_ReadEncodedImage("texture", encoded));
    CHECK(!encoded.data); // A broken preferred candidate must retain synchronous fallback.
    TextureWorkers workers;
    CHECK(workers.start(4));
    std::atomic<int> completed(0);
    const auto pngSource = read(std::string(directory) + "/rgba16.png");
    for (int i = 0; i < 100; ++i) workers.submit([&]() {
        DecodedImage result;
        CHECK(pngTest::R_DecodePNG(pngSource.data(), pngSource.size(), allocator, result, nullptr, 0));
        std::free(result.pixels); ++completed;
    });
    // Wait for submitted work through an actual barrier, then exercise reuse.
    while (completed.load() != 100) std::this_thread::yield();
    workers.shutdown();
    CHECK(workers.start(2));
    std::mutex mutex; std::condition_variable wake; bool active = false, release = false;
    workers.submit([&]() {
        std::unique_lock<std::mutex> lock(mutex); active = true; wake.notify_one();
        wake.wait(lock, [&]() { return release; });
    });
    { std::unique_lock<std::mutex> lock(mutex); wake.wait(lock, [&]() { return active; }); }
    // A shutdown in another thread must wait for a worker that still owns input.
    std::atomic<bool> joined(false);
    std::thread shutdown([&]() { workers.shutdown(); joined = true; });
    { std::lock_guard<std::mutex> lock(mutex); CHECK(!joined.load()); release = true; }
    wake.notify_all(); shutdown.join(); CHECK(joined.load());
    waveTest::check();
}
}
