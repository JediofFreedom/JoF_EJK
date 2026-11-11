#include "vk_prepared_cache.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif
#define CHECK(v) do { if (!(v)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #v); std::exit(1); } } while (0)
using namespace PreparedTextures;
static const uint32_t AllFormats = 15;

namespace pathTest {
typedef int qboolean;
enum {qfalse, qtrue, MAX_OSPATH = 256};
static struct { const char *string; } home = {"C:/portable"}, *fs_homepath = &home;
static const char *fs_gamedir = "EternalJK";
static void FS_AssertInitialised() {}
static const char *FS_BuildOSPath(const char *base, const char *game, const char *file) {
    static std::string result; result = std::string(base) + "/" + game + "/" + file; return result.c_str();
}
static void Q_strncpyz(char *out, const char *in, size_t size) { std::snprintf(out, size, "%s", in); }
#include "write_path.h"
static void check() {
    char path[256];
    CHECK(FS_GetWritePath("cache/vulkan/a.dat", path, sizeof(path)));
    CHECK(std::string(path) == "C:/portable/EternalJK/cache/vulkan/a.dat");
    home.string = "D:/user profile"; fs_gamedir = "custommod";
    CHECK(FS_GetWritePath("cache/vulkan/a.dat", path, sizeof(path)));
    CHECK(std::string(path) == "D:/user profile/custommod/cache/vulkan/a.dat");
    for (const char *bad : {"", "../a", "cache/../../a", "/absolute", "\\absolute", "C:/absolute"}) {
        std::strcpy(path, "unchanged"); CHECK(!FS_GetWritePath(bad, path, sizeof(path)) && path[0] == 0);
    }
    CHECK(!FS_GetWritePath("cache/a", path, 2) && path[0] == 0);
    const std::string longName(240, 'a');
    CHECK(!FS_GetWritePath(longName.c_str(), path, sizeof(path)) && path[0] == 0);
}
}

static void mkdir(const std::string &path) {
#ifdef _WIN32
    CHECK(_mkdir(path.c_str()) == 0);
#else
    CHECK(::mkdir(path.c_str(), 0700) == 0);
#endif
}
static void rmdir(const std::string &path) {
#ifdef _WIN32
    CHECK(_rmdir(path.c_str()) == 0);
#else
    CHECK(::rmdir(path.c_str()) == 0);
#endif
}
static void write(const std::string &path, const void *bytes, size_t size) {
    FILE *file = std::fopen(path.c_str(), "wb"); CHECK(file);
    CHECK(std::fwrite(bytes, 1, size, file) == size); CHECK(std::fclose(file) == 0);
}
static Key keyFor(unsigned i) { return ContentKey(&i, sizeof(i), "settings", 8); }
int main(int argc, char **argv) {
    CHECK(argc == 2);
    pathTest::check();
    const Key key = ContentKey("source", 6, "settings", 8);
    CHECK(key == ContentKey("source", 6, "settings", 8));
    CHECK(!(key == ContentKey("source!", 7, "settings", 8)));
    CHECK(!(key == ContentKey("source", 6, "settings!", 9)));
    CHECK(!(ContentKey("ab", 2, "c", 1) == ContentKey("a", 1, "bc", 2)));
    CHECK(FileName(key).size() == 36);
    for (Format format : {Format::RGBA8, Format::RGBA4444, Format::RGB5551, Format::BC3}) {
        const Description description = {8, 8, 8, 8, 4, format};
        const size_t bytes = PayloadSize(description);
        CHECK(bytes == (format == Format::BC3 ? 112 : 340));
        std::vector<unsigned char> data(bytes, 0x57);
        unsigned char header[HeaderSize];
        CHECK(WriteHeader(header, description, key, data.data(), data.size()));
        Description read;
        CHECK(ReadHeader(header, HeaderSize + bytes, key, 8, 8, AllFormats, read));
        CHECK(read.format == format && read.levels == 4);
        CHECK(VerifyPayload(header, data.data(), data.size()));
        for (size_t i = 0; i < HeaderSize; ++i) {
            header[i] ^= 1; CHECK(!ReadHeader(header, HeaderSize + bytes, key, 8, 8, AllFormats, read)); header[i] ^= 1;
        }
        data[bytes / 2] ^= 1; CHECK(!VerifyPayload(header, data.data(), data.size()));
        CHECK(!ReadHeader(header, HeaderSize + bytes - 1, key, 8, 8, AllFormats, read));
        CHECK(!ReadHeader(header, HeaderSize + bytes, keyFor(1), 8, 8, AllFormats, read));
        CHECK(!ReadHeader(header, HeaderSize + bytes, key, 16, 8, AllFormats, read));
        CHECK(!ReadHeader(header, HeaderSize + bytes, key, 8, 8, 0, read));
    }
    CHECK(PayloadSize({1,8,1,8,4,Format::RGBA4444}) == 60);
    CHECK(!PayloadSize({0,8,8,8,4,Format::RGBA8}));
    CHECK(!PayloadSize({8,8,16,8,4,Format::RGBA8}));
    CHECK(!PayloadSize({8,8,8,8,2,Format::RGBA8}));
    CHECK(!PayloadSize({8,8,8,8,5,Format::RGBA8}));
    CHECK(!PayloadSize({8,8,8,8,0,Format::RGBA8}));
    CHECK(!PayloadSize({32768,32768,32768,32768,16,Format::RGBA8}));
    CHECK(!PayloadSize({8,8,8,8,4,static_cast<Format>(32)}));

    const std::string directory = std::string(argv[1]) + "/cache-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    mkdir(directory);
    const Description description = {64,64,64,64,7,Format::RGBA8};
    const std::vector<unsigned char> bytes(PayloadSize(description), 0x42);
    const size_t entrySize = HeaderSize + bytes.size();
    std::vector<Key> keys;
    for (unsigned i = 0; i < 6; ++i) {
        keys.push_back(keyFor(i));
        Cache cache;
        CHECK(cache.configure(directory, entrySize * 3, entrySize * 2));
        CHECK(cache.store(keys.back(), description, bytes.data(), bytes.size()));
        cache.shutdown();
        CHECK(cache.statistics().queuedBytes == 0 && cache.statistics().diskBytes <= entrySize * 3);
        Texture texture;
        CHECK(cache.read(keys.back(), 64,64, AllFormats, texture) && texture.bytes == bytes);
    }
    {
        Cache writer;
        CHECK(writer.configure(directory, entrySize * 3, entrySize * 2));
        Cache reader;
        CHECK(reader.configure(directory, entrySize * 3, entrySize * 2));
        CHECK(!reader.store(keyFor(100), description, bytes.data(), bytes.size()));
        Texture texture;
        CHECK(reader.read(keys.back(), 64,64, AllFormats, texture) && texture.bytes == bytes);
        unsigned retained = 0;
        for (const auto &candidate : keys) retained += reader.read(candidate,64,64,AllFormats,texture) ? 1 : 0;
        CHECK(retained <= 3);
        // Independent cache instances represent process restarts, while the
        // writer lock leaves another running client safely read-only.
        reader.shutdown(); writer.shutdown();
    }
    const std::string file = directory + "/" + FileName(keys.back());
    unsigned char corrupt = 0;
    write(file, &corrupt, 1);
    {
        Cache cache; CHECK(cache.configure(directory, entrySize * 3, entrySize / 2));
        Texture texture; CHECK(!cache.read(keys.back(),64,64,AllFormats,texture) && texture.bytes.empty());
        CHECK(cache.statistics().invalid == 1);
        CHECK(!cache.store(keys.back(), description, bytes.data(), bytes.size()));
        CHECK(cache.statistics().droppedWrites == 1);
        cache.shutdown();
    }
    // Stale temporary files from an interrupted write are reclaimed under the
    // writer lock; unrelated files in the same directory remain untouched.
    const std::string stale = directory + "/" + FileName(keys.front()).substr(0,32) + ".tmp-123";
    write(stale, &corrupt, 1);
    const std::string unrelated = directory + "/unrelated.dat"; write(unrelated, &corrupt, 1);
    {
        Cache cache; CHECK(cache.configure(directory, entrySize * 3, entrySize * 2)); cache.shutdown();
        CHECK(!std::fopen(stale.c_str(), "rb"));
        FILE *retained = std::fopen(unrelated.c_str(), "rb"); CHECK(retained); std::fclose(retained);
    }
    {
        Cache cache; CHECK(cache.configure(directory, entrySize * 3, entrySize * 64));
        std::atomic<bool> stop(false);
        std::atomic<unsigned> hits(0);
        std::thread reader([&]() {
            while (!stop.load()) {
                Texture texture;
                if (cache.read(keys.back(),64,64,AllFormats,texture)) {
                    CHECK(texture.bytes.size() == bytes.size());
                    CHECK(std::all_of(texture.bytes.begin(), texture.bytes.end(), [&](unsigned char v) { return v == texture.bytes.front(); }));
                    ++hits;
                }
            }
        });
        for (unsigned i = 0; i < 32; ++i) {
            const std::vector<unsigned char> pixels(bytes.size(), static_cast<unsigned char>(i));
            CHECK(cache.store(keys.back(),description,pixels.data(),pixels.size()));
        }
        cache.shutdown(); stop.store(true); reader.join();
        // Every successful concurrent read saw a whole published entry; a busy
        // filesystem may make replacement fail, which safely remains a miss.
        CHECK(cache.statistics().queuedBytes == 0);
    }
    for (const auto &entry : keys) std::remove((directory + "/" + FileName(entry)).c_str());
    CHECK(std::remove(unrelated.c_str()) == 0);
    CHECK(std::remove((directory + "/writer.lock").c_str()) == 0);
    rmdir(directory);
    std::puts("Prepared texture cache format, corruption, persistence, budgets, and writer isolation passed.");
}
