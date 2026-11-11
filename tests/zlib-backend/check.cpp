#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
#include <zlib.h>
#include <minizip/unzip.h>
#include <png.h>
#include "tr_image_decode.h"
using byte = unsigned char;
#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " #condition); } while (0)
extern "C" void *openjk_minizip_malloc(int size) { return std::malloc(size); }
extern "C" int openjk_minizip_free(void *memory) { std::free(memory); return 0; }
#include "png_decoder.h"

static std::vector<byte> read(const std::string &path) {
    std::ifstream input(path.c_str(), std::ios::binary);
    CHECK(input.good());
    return std::vector<byte>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}
static int liveCodecAllocations;
static voidpf allocateCodec(voidpf, uInt count, uInt size) {
    void *memory = std::calloc(count, size);
    CHECK(memory);
    ++liveCodecAllocations;
    return memory;
}
static void releaseCodec(voidpf, voidpf memory) {
    if (memory) --liveCodecAllocations;
    std::free(memory);
}
static void streaming(const std::string &root) {
    const auto raw = read(root + "/raw.bin"), dictionary = read(root + "/dictionary.bin");
    const char *files[] = {"raw.deflate", "wrapped.zlib", "wrapped.gzip", "dictionary.zlib"};
    const int bits[] = {-15, 15, 31, 15};
    for (size_t i = 0; i < 4; ++i) {
        auto input = read(root + "/" + files[i]);
        z_stream stream = {};
        stream.zalloc = allocateCodec;
        stream.zfree = releaseCodec;
        CHECK(inflateInit2(&stream, bits[i]) == Z_OK);
        for (size_t chunk : {size_t(1), size_t(17), size_t(4096), size_t(65536)}) {
            CHECK(inflateReset(&stream) == Z_OK);
            size_t fed = 0, iterations = 0;
            std::vector<byte> output, buffer(chunk);
            int status = Z_OK;
            while (status != Z_STREAM_END) {
                CHECK(++iterations < 2000000);
                if (!stream.avail_in && fed < input.size()) {
                    size_t count = std::min(chunk, input.size() - fed);
                    stream.next_in = input.data() + fed;
                    stream.avail_in = static_cast<uInt>(count);
                    fed += count;
                }
                stream.next_out = buffer.data();
                stream.avail_out = static_cast<uInt>(buffer.size());
                status = inflate(&stream, Z_NO_FLUSH);
                output.insert(output.end(), buffer.begin(), buffer.end() - stream.avail_out);
                if (status == Z_NEED_DICT) {
                    CHECK(i == 3);
                    CHECK(inflateSetDictionary(&stream, dictionary.data(), static_cast<uInt>(dictionary.size())) == Z_OK);
                    status = Z_OK;
                }
                CHECK(status == Z_OK || status == Z_BUF_ERROR || status == Z_STREAM_END);
                CHECK(status == Z_STREAM_END || stream.avail_in || fed < input.size() || stream.avail_out < buffer.size());
            }
            CHECK(output == raw && stream.total_out == raw.size());
        }
        CHECK(inflateEnd(&stream) == Z_OK && !liveCodecAllocations);
    }
    auto wrapped = read(root + "/wrapped.zlib"), corrupt = wrapped;
    std::vector<byte> output(raw.size() + 1);
    uLongf outputSize = static_cast<uLongf>(output.size());
    corrupt.back() ^= 1;
    CHECK(uncompress(output.data(), &outputSize, corrupt.data(), static_cast<uLong>(corrupt.size())) == Z_DATA_ERROR);
    outputSize = static_cast<uLongf>(output.size());
    CHECK(uncompress(output.data(), &outputSize, wrapped.data(), static_cast<uLong>(wrapped.size() - 1)) == Z_DATA_ERROR);
    uLongf encodedSize = compressBound(static_cast<uLong>(raw.size()));
    std::vector<byte> encoded(encodedSize);
    CHECK(compress2(encoded.data(), &encodedSize, raw.data(), static_cast<uLong>(raw.size()), 6) == Z_OK);
    outputSize = static_cast<uLongf>(output.size());
    CHECK(uncompress(output.data(), &outputSize, encoded.data(), encodedSize) == Z_OK);
    output.resize(outputSize);
    CHECK(output == raw);
    CHECK(crc32(0, reinterpret_cast<const Bytef *>("123456789"), 9) == 0xcbf43926UL);
}
static void zip(const std::string &root) {
    const auto expected = read(root + "/raw.bin");
    unzFile archive = unzOpen64((root + "/stream.pk3").c_str());
    CHECK(archive);
    for (const char *name : {"compressed.bin", "stored.bin", "empty"}) {
        CHECK(unzLocateFile(archive, name, 1) == UNZ_OK);
        const ZPOS64_T offset = unzGetOffset64(archive);
        CHECK(unzOpenCurrentFile(archive) == UNZ_OK);
        std::vector<byte> output;
        byte buffer[17];
        int count;
        while ((count = unzReadCurrentFile(archive, buffer, sizeof(buffer))) > 0)
            output.insert(output.end(), buffer, buffer + count);
        CHECK(count == 0 && (std::strcmp(name, "empty") ? output == expected : output.empty()));
        CHECK(unzCloseCurrentFile(archive) == UNZ_OK);
        CHECK(unzSetOffset64(archive, offset) == UNZ_OK);
        CHECK(unzOpenCurrentFile(archive) == UNZ_OK);
        CHECK(unzReadCurrentFile(archive, buffer, 1) == (std::strcmp(name, "empty") ? 1 : 0));
        CHECK(unzCloseCurrentFile(archive) == UNZ_OK);
    }
    CHECK(unzClose(archive) == UNZ_OK);
    archive = unzOpen64((root + "/crc-bad.pk3").c_str());
    CHECK(archive && unzOpenCurrentFile(archive) == UNZ_OK);
    byte buffer[4096];
    int count;
    while ((count = unzReadCurrentFile(archive, buffer, sizeof(buffer))) > 0) {}
    CHECK(count == 0 && unzCloseCurrentFile(archive) == UNZ_CRCERROR);
    CHECK(unzClose(archive) == UNZ_OK);
}
static int liveImages;
static bool failImageAllocation;
static void *allocateImage(size_t size) {
    if (failImageAllocation) return NULL;
    void *memory = std::malloc(size);
    if (memory) ++liveImages;
    return memory;
}
static void releaseImage(void *memory) { if (memory) --liveImages; std::free(memory); }
static void png(const std::string &root) {
    const ImageAllocator allocator = {allocateImage, releaseImage};
    for (const char *name : {"rgba", "rgba16", "palette", "gray-low", "adam7"}) {
        const auto encoded = read(root + "/" + name + ".png"), expected = read(root + "/" + name + ".rgba");
        DecodedImage image = {};
        char error[256] = {};
        CHECK(R_DecodePNG(encoded.data(), encoded.size(), allocator, image, error, sizeof(error)));
        CHECK(image.width == 2 && image.height == 2 && liveImages == 1);
        CHECK(std::memcmp(image.pixels, expected.data(), expected.size()) == 0);
        releaseImage(image.pixels);
        CHECK(!liveImages);
        failImageAllocation = true;
        CHECK(!R_DecodePNG(encoded.data(), encoded.size(), allocator, image, error, sizeof(error)));
        CHECK(!image.pixels && !image.width && !image.height && !liveImages);
        failImageAllocation = false;
    }
    for (const char *name : {"truncated.png", "crc-bad.png"}) {
        const auto encoded = read(root + "/" + name);
        DecodedImage image = {};
        char error[256] = {};
        CHECK(!R_DecodePNG(encoded.data(), encoded.size(), allocator, image, error, sizeof(error)));
        CHECK(!image.pixels && !image.width && !image.height && !liveImages && error[0]);
    }
}
int main(int argc, char **argv) {
    try {
        CHECK(argc == 2);
        CHECK((std::strstr(zlibVersion(), "zlib-ng") != NULL) == bool(EXPECT_ZLIB_NG));
        streaming(argv[1]);
        zip(argv[1]);
        png(argv[1]);
        std::printf("%s: streaming/reset/dictionary/errors/allocators, actual minizip CRC/reopen, production PNG formats/errors passed\n", zlibVersion());
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
