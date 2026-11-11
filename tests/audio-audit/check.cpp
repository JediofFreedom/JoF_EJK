#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)
typedef unsigned char byte;
typedef int qboolean;
typedef int fileHandle_t;
enum { qfalse, qtrue, WAV_FORMAT_PCM = 1, FS_SEEK_CUR = 1 };
struct sfx_t { int iSoundLengthInSamples; short *pSoundData; float fVolRange; };
static struct { int speed; } dma;
static int allocations;
static byte *SND_malloc(int size, sfx_t *) {
    CHECK(size > 0); ++allocations; return static_cast<byte *>(std::malloc(size));
}
static short LittleShort(short value) {
    const unsigned endian = 1;
    if (*reinterpret_cast<const byte *>(&endian)) return value;
    const unsigned v = static_cast<unsigned short>(value);
    return static_cast<short>((v >> 8) | (v << 8));
}
static std::vector<byte> fileData;
static size_t position, readBytes;
static int readCalls, failRead;
static int FS_FTell(int) { return int(position); }
static int FS_Seek(int, long offset, int origin) {
    CHECK(origin == FS_SEEK_CUR);
    const int64_t target = int64_t(position) + offset;
    if (target < 0 || uint64_t(target) > fileData.size()) return -1;
    position = size_t(target); return 0;
}
static int FS_Read(void *out, int length, int) {
    ++readCalls;
    const size_t count = failRead == readCalls ? 0 : std::min(size_t(length), fileData.size() - position);
    std::memcpy(out, fileData.data() + position, count);
    position += count; readBytes += count; return int(count);
}
#include "wave_type.h"
#include "production.h"

static void freeSound(sfx_t &sound) {
    if (sound.pSoundData) { --allocations; std::free(sound.pSoundData); sound.pSoundData = nullptr; }
}
static void resampling() {
    // Compare established 8-bit-fraction conversion for normal rates, including
    // non-integral steps. Volume calculation must match as well as PCM bytes.
    for (int rate : {11025, 22050, 32000, 44100, 48000}) for (int output : {22050, 44100, 48000})
        for (int width : {1, 2}) {
            const int samples = 257;
            std::vector<byte> input(samples * width);
            for (size_t i = 0; i < input.size(); ++i) input[i] = byte(i * 79);
            sfx_t sound = {samples, nullptr, 0}; dma.speed = output;
            ResampleSfx(&sound, rate, width, input.data());
            const float scale = float(rate) / output;
            CHECK(sound.iSoundLengthInSamples == int(samples / scale));
            unsigned fraction = 0, step = unsigned(scale * 256);
            int volume = 0;
            for (int i = 0; i < sound.iSoundLengthInSamples; ++i) {
                const size_t source = fraction >> 8; fraction += step;
                CHECK(source < size_t(samples));
                int expected;
                if (width == 2) {
                    const unsigned v = unsigned(input[source * 2]) | (unsigned(input[source * 2 + 1]) << 8);
                    expected = v >= 32768 ? int(v) - 65536 : int(v);
                } else expected = (int(input[source]) - 128) * 256;
                CHECK(sound.pSoundData[i] == expected);
                volume = std::max(volume, std::abs(expected) >> 8);
            }
            CHECK(sound.fVolRange == volume); freeSound(sound);
        }
    // A 32-bit phase used to wrap exactly here and repeat sample zero.
    std::vector<byte> longSound((1u << 24) + 3, 128);
    longSound[0] = 12; longSound[1u << 24] = 210; longSound.back() = 250;
    sfx_t sound = {int(longSound.size()), nullptr, 0}; dma.speed = 22050;
    ResampleSfx(&sound, 22050, 1, longSound.data());
    CHECK(sound.pSoundData[1u << 24] == (210 - 128) * 256);
    CHECK(sound.pSoundData[longSound.size() - 1] == (250 - 128) * 256); freeSound(sound);
    // Low positive input rates must advance rather than repeat sample zero.
    byte slow[] = {12, 210}; sound.iSoundLengthInSamples = 2; dma.speed = 1000;
    ResampleSfx(&sound, 1, 1, slow);
    CHECK(sound.iSoundLengthInSamples == 1999 || sound.iSoundLengthInSamples == 2000);
    CHECK(sound.pSoundData[1000] == (210 - 128) * 256); freeSound(sound);
    for (int badRate : {0, -1}) {
        sound.iSoundLengthInSamples = 2; dma.speed = 22050;
        ResampleSfx(&sound, badRate, 1, slow);
        CHECK(!sound.pSoundData && !sound.iSoundLengthInSamples);
    }
    sound.iSoundLengthInSamples = INT_MAX; dma.speed = INT_MAX;
    ResampleSfx(&sound, 1, 1, slow); // Reject overflowing output allocation before touching input.
    CHECK(!sound.pSoundData && !sound.iSoundLengthInSamples);
    sound.iSoundLengthInSamples = 2; dma.speed = 0;
    ResampleSfx(&sound, 22050, 1, slow); CHECK(!sound.pSoundData);
    sound.iSoundLengthInSamples = 2; dma.speed = 22050;
    ResampleSfx(&sound, 22050, 3, slow); CHECK(!sound.pSoundData);
}
static void put32(std::vector<byte> &out, size_t offset, unsigned value) {
    for (unsigned i = 0; i < 4; ++i) out[offset + i] = byte(value >> (i * 8));
}
static void chunk(std::vector<byte> &out, const char *name, const std::vector<byte> &data) {
    out.insert(out.end(), name, name + 4);
    const size_t offset = out.size(); out.resize(offset + 4); put32(out, offset, unsigned(data.size()));
    out.insert(out.end(), data.begin(), data.end()); if (data.size() & 1) out.push_back(0);
}
static qboolean parse(wavinfo_t &info, long declaredLength = -1) {
    position = readBytes = readCalls = 0;
    return S_ReadMusicWavHeader(1, declaredLength < 0 ? long(fileData.size()) : declaredLength, info);
}
static void streamedWaves() {
    for (int width : {1, 2}) for (int channels : {1, 2}) for (bool dataFirst : {false, true}) {
        fileData = {'R','I','F','F',0,0,0,0,'W','A','V','E'};
        chunk(fileData, "JUNK", {1,2,3});
        const std::vector<byte> pcm(width == 1 && channels == 1 ? 127 : 128, 0x35);
        if (dataFirst) chunk(fileData, "data", pcm);
        const size_t formatOffset = fileData.size() + 8;
        // WAVEFORMATEX's two extra bytes must not be mistaken for the data tag.
        chunk(fileData, "fmt ", {1,0,byte(channels),0,0x22,0x56,0,0,0,0,0,0,byte(width*channels),0,byte(width*8),0,0,0});
        chunk(fileData, "LIST", {1,2,3,4,5});
        if (!dataFirst) chunk(fileData, "data", pcm);
        put32(fileData, 4, unsigned(fileData.size() - 8));
        const auto original = fileData;
        wavinfo_t info;
        CHECK(parse(info));
        CHECK(info.channels == channels && info.width == width && info.rate == 22050);
        CHECK(info.samples == int(pcm.size()) / (width * channels));
        CHECK(position == size_t(info.dataofs) && readBytes < 100);
        byte first; CHECK(FS_Read(&first, 1, 1) == 1 && first == 0x35);
        for (size_t cut = 0; cut < original.size(); ++cut) {
            fileData.assign(original.begin(), original.begin() + cut);
            CHECK(!parse(info));
        }
        fileData = original;
        for (int fault = 1; fault <= 3; ++fault) {
            failRead = fault; CHECK(!parse(info));
        }
        failRead = 0;
        for (byte bad : {byte(0), byte(7), byte(24), byte(32)}) {
            fileData = original; fileData[formatOffset + 14] = bad; CHECK(!parse(info));
        }
        fileData = original; put32(fileData, formatOffset + 4, 0); CHECK(!parse(info));
        fileData = original; fileData[formatOffset + 2] = 0; CHECK(!parse(info));
        fileData = original; put32(fileData, 16, 0xffffffffU); CHECK(!parse(info));
    }
    CHECK(S_BackgroundReadSamples(16384, INT_MAX, 22050, INT_MAX, 4, 30000) == 7500);
    CHECK(S_BackgroundReadSamples(16384, 22050, 22050, 256, 4, 30000) == 256);
    CHECK(S_BackgroundReadSamples(16384, 22050, 44100, INT_MAX, 4, 30000) == 7500);
    CHECK(S_BackgroundReadSamples(16384, 22050, 0, INT_MAX, 4, 30000) == 0);
    CHECK(S_BackgroundReadSamples(16384, 22050, 22050, INT_MAX, 0, 30000) == 0);
}
int main() {
    resampling(); streamedWaves(); CHECK(allocations == 0);
    std::puts("Audio parsing and resampling regressions passed.");
}
