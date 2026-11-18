#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "bit_mask.h"
#define STBI_ONLY_HDR
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define CHECK(v) do { if (!(v)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #v); std::exit(1); } } while (0)
typedef unsigned char byte;
static const int PRINT_DEVELOPER = 0;
static std::vector<byte> input;
static unsigned fileBuffers, errors;
static bool missing;
static struct {
    static int FS_ReadFile(const char *, void **out) {
        if (missing) { *out = nullptr; return -1; }
        *out = std::malloc(input.empty() ? 1 : input.size()); CHECK(*out); ++fileBuffers;
        std::memcpy(*out, input.data(), input.size()); return int(input.size());
    }
    static void FS_FreeFile(void *p) { std::free(p); --fileBuffers; }
    static void Printf(int, const char *, ...) { ++errors; }
} ri;
#include "hdr.h"
int main() {
    CHECK(Q_BitMask32(-1) == 0 && Q_BitMask32(0) == 0);
    for (int count = 1; count <= 32; ++count) {
        const uint64_t expected = (UINT64_C(1) << count) - 1;
        CHECK(Q_BitMask32(count) == expected);
    }
    CHECK(Q_BitMask32(33) == UINT32_MAX);
    const char header[] = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 2\n";
    input.assign(header, header + std::strlen(header));
    // Four pixels in the old (non-scanline-RLE) RGBE encoding.
    input.insert(input.end(), {128,64,32,129, 64,32,16,129, 32,16,8,129, 16,8,4,129});
    byte *pixels = nullptr; int width = 0, height = 0;
    R_LoadHDRImage("valid.hdr", &pixels, &width, &height);
    CHECK(pixels && width == 2 && height == 2 && fileBuffers == 0);
    const float *values = reinterpret_cast<float *>(pixels);
    CHECK(values[0] == 1.f && values[1] == .5f && values[2] == .25f);
    stbi_image_free(pixels);
    R_LoadHDRImage("optional-dimensions.hdr", &pixels, nullptr, nullptr);
    CHECK(pixels && fileBuffers == 0); stbi_image_free(pixels);
    R_LoadHDRImage("no-output.hdr", nullptr, nullptr, nullptr); CHECK(fileBuffers == 0);
    input = {'b','a','d'}; width = height = 123;
    R_LoadHDRImage("bad.hdr", &pixels, &width, &height);
    CHECK(!pixels && !width && !height && errors == 1 && fileBuffers == 0);
    input.clear(); R_LoadHDRImage("empty.hdr", &pixels, nullptr, nullptr); CHECK(!pixels && fileBuffers == 0);
    missing = true; R_LoadHDRImage("missing.hdr", &pixels, &width, &height); CHECK(!pixels && !width && !height);
}
