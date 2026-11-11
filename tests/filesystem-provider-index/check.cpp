#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <minizip/unzip.h>
#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #value); std::exit(1); } } while (0)
#define ARRAY_LEN(value) (sizeof(value) / sizeof((value)[0]))
#define S_COLOR_YELLOW ""
typedef unsigned char byte;
typedef int qboolean;
enum { qfalse, qtrue, MAX_QPATH = 64 };
struct fsReadFileIdentity_t { uint64_t generation, container, position, size; uint32_t provider, reserved; };
static void Q_strncpyz(char *out, const char *in, size_t size) { std::snprintf(out, size, "%s", in); }
static int Q_stricmp(const char *a, const char *b) {
    for (;;) {
        const int x = std::tolower(static_cast<unsigned char>(*a++)), y = std::tolower(static_cast<unsigned char>(*b++));
        if (x != y) return x - y;
        if (!x) return 0;
    }
}
static void Com_Printf(const char *, ...) {}
extern "C" void *openjk_minizip_malloc(int size) { return std::malloc(size); }
extern "C" int openjk_minizip_free(void *memory) { std::free(memory); return 0; }
#define EJK_PROVIDER_PRODUCTION_HASH
#include "../loading-audit/fs_checks.h"

namespace filesystemTest {
static bool fs_reordered;
static int fs_serverPaks[4096];
#include "reorder.h"

struct Mount {
    std::vector<Entry> entries;
    std::vector<Entry *> heads;
    Pack pack = {};
    Search path = {};
    Mount(size_t count, int id) : entries(count), heads(1024) {
        pack.hashSize = int(heads.size()); pack.hashTable = heads.data();
        pack.pure = true; pack.sourceIdentity = id; pack.checksum = id;
        path.pack = &pack;
    }
    void name(size_t i, const char *value, int length) {
        Q_strncpyz(entries[i].name, value, sizeof(entries[i].name)); entries[i].len = length;
    }
    void link() {
        std::fill(heads.begin(), heads.end(), nullptr);
        for (auto &entry : entries) {
            const size_t bucket = FS_HashFileName(entry.name, pack.hashSize);
            entry.next = heads[bucket]; heads[bucket] = &entry;
        }
    }
};
static void install(Search *head, int indexed = 1) {
    fs_pakLookupIndex.reset(); fs_searchpaths = head; lookupSetting.integer = indexed;
    fs_numServerPaks = 0;
}
static fsReadFileIdentity_t query(const char *name, long expected, bool referenced = false) {
    fsReadFileIdentity_t identity = {};
    const int allocated = handleAllocations;
    CHECK(FS_GetReadFileIdentity(name, &identity, referenced ? qtrue : qfalse) == (expected >= 0));
    if (expected >= 0) CHECK(identity.size == uint64_t(expected));
    CHECK(handleAllocations == allocated); return identity;
}
static void write(const std::string &name, const char *text) {
    FILE *file = std::fopen(name.c_str(), "wb"); CHECK(file);
    CHECK(std::fwrite(text, 1, std::strlen(text), file) == std::strlen(text)); std::fclose(file);
}
static void providers(const char *root) {
    for (int probe : {0, 1}) for (int indexed : {0, 1}) {
        metadataProbeSetting.integer = probe;
        Mount high(3, 10), low(2, 20);
        high.name(0, "same.jpg", 9); high.name(1, "dir/file.jpg", 11); high.name(2, "autoexec.cfg", 99);
        low.name(0, "same.jpg", 7); low.name(1, "low.jpg", 12); high.link(); low.link();
        Directory directory = {}; Q_strncpyz(directory.path, root, sizeof(directory.path));
        Directory middleDirectory = {}; const std::string middlePath = std::string(root) + "/middle";
        Q_strncpyz(middleDirectory.path, middlePath.c_str(), sizeof(middleDirectory.path));
        Search middle = {&low.path, nullptr, &middleDirectory};
        Search loose = {&high.path, nullptr, &directory}; high.path.next = &middle;
        install(&loose, indexed);
        CHECK(query("same.jpg", 9).generation == 10);
        const std::string middleLoose = middlePath + "/low.jpg";
        const std::string behindHighPak = middlePath + "/same.jpg";
        write(middleLoose, "middle override"); CHECK(query("low.jpg", 15).provider == 2);
        write(behindHighPak, "lower directory"); CHECK(query("same.jpg", 9).provider == 1);
        const size_t builds = fs_pakLookupIndex.buildCount();
        const std::string live = std::string(root) + "/same.jpg";
        write(live, "loose override"); CHECK(query("same.jpg", 14).provider == 2);
        write(live, "changed"); CHECK(query("same.jpg", 7).provider == 2);
        CHECK(!std::remove(live.c_str())); CHECK(query("same.jpg", 9).provider == 1);
        fs_numServerPaks = 2; high.pack.pure = false;
        CHECK(query("low.jpg", 12).provider == 1); // Pure filters still bypass interleaved loose images.
        CHECK(query("same.jpg", 7, true).generation == 20 && low.pack.referenced == FS_GENERAL_REF);
        CHECK(high.pack.referenced == 0); low.pack.pure = false; query("same.jpg", -1);
        high.pack.pure = true; CHECK(query("same.jpg", 9).generation == 10);
        CHECK(fs_pakLookupIndex.buildCount() == builds); // Pure membership remains a live check.
        fs_numServerPaks = 0;
        query("autoexec.cfg", -1); // Config files in archives never win.
        CHECK(query("DIR\\FILE.JPG", 11).generation == 10);
        query("dir:file.jpg", -1); // Preserve legacy per-pak hash eligibility for colon aliases.
        const std::string missing = std::string(root) + "/live.dat";
        query("live.dat", -1); write(missing, ""); query("live.dat", 0);
        fs_numServerPaks = 1; write(missing, "new journal"); CHECK(query("live.dat", 11).provider == 2);
        CHECK(!std::remove(missing.c_str())); query("live.dat", -1);
        CHECK(!std::remove(middleLoose.c_str()) && !std::remove(behindHighPak.c_str()));
        fs_numServerPaks = 0; fs_pakLookupIndex.reset(); fs_searchpaths = nullptr;
    }
}
static void metadataProbes(const char *root) {
    const std::string live = std::string(root) + "/metadata-probe.jpg";
    const std::string missingParent = std::string(root) + "/missing-parent/metadata-probe.jpg";
    for (int probe : {0, 1}) for (int indexed : {0, 1}) {
        metadataProbeSetting.integer = probe;
        Mount mount(1, 10); mount.name(0, "metadata-probe.jpg", 9); mount.link();
        Directory directory = {}; Q_strncpyz(directory.path, root, sizeof(directory.path));
        Search loose = {&mount.path, nullptr, &directory}; install(&loose, indexed);
        looseReadOpens = 0;
        CHECK(query("metadata-probe.jpg", 9, true).provider == 1);
#ifdef _WIN32
        CHECK(looseReadOpens == (probe ? 0u : 1u));
        CHECK(FS_LooseFileDefinitelyMissing(live.c_str()));
        CHECK(FS_LooseFileDefinitelyMissing(missingParent.c_str()));
        CHECK(!FS_LooseFileDefinitelyMissing((std::string(root) + "/NUL.jpg").c_str()));
        CHECK(!FS_LooseFileDefinitelyMissing((std::string(root) + "/lpt1.txt").c_str()));
        CHECK(!FS_LooseFileDefinitelyMissing((std::string(root) + "/missing-\xc3\xa9.jpg").c_str()));
        // Invalid names are uncertain errors, not negative-cache evidence.
        CHECK(!FS_LooseFileDefinitelyMissing((std::string(root) + "/invalid|name.jpg").c_str()));
#else
        CHECK(looseReadOpens == 1u);
#endif
        CHECK(mount.pack.referenced == FS_GENERAL_REF);
        write(live, "live override"); looseReadOpens = 0;
        CHECK(!FS_LooseFileDefinitelyMissing(live.c_str()));
        CHECK(query("metadata-probe.jpg", 13).provider == 2 && looseReadOpens == 1);
        write(live, ""); CHECK(query("metadata-probe.jpg", 0).provider == 2);
#ifdef _WIN32
        HANDLE blocked = CreateFileA(live.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        CHECK(blocked != INVALID_HANDLE_VALUE);
        looseReadOpens = 0;
        CHECK(query("metadata-probe.jpg", 9).provider == 1 && looseReadOpens == 1);
        CloseHandle(blocked);
        CHECK(query("metadata-probe.jpg", 0).provider == 2);
#endif
        CHECK(!std::remove(live.c_str())); CHECK(query("metadata-probe.jpg", 9).provider == 1);
        fs_pakLookupIndex.reset(); fs_searchpaths = nullptr;
    }
}
static void reorder() {
    Mount first(1, 10), second(1, 20);
    first.name(0, "same.jpg", 9); second.name(0, "same.jpg", 7); first.link(); second.link();
    first.path.next = &second.path; install(&first.path);
    CHECK(query("same.jpg", 9).generation == 10);
    const size_t builds = fs_pakLookupIndex.buildCount();
    fs_numServerPaks = 2; fs_serverPaks[0] = 20; fs_serverPaks[1] = 10;
    FS_ReorderPurePaks(); CHECK(fs_reordered && fs_searchpaths == &second.path);
    CHECK(query("same.jpg", 7).generation == 20 && fs_pakLookupIndex.buildCount() == builds + 1);
    second.path.next = nullptr; first.path.next = &second.path; fs_searchpaths = &first.path;
    fs_pakLookupIndex.invalidate(); fs_numServerPaks = 0;
    CHECK(query("same.jpg", 9).generation == 10);
    fs_pakLookupIndex.reset(); fs_searchpaths = nullptr;
}
static void duplicateOpen(const char *root) {
    const std::string zip = std::string(root) + "/duplicates.pk3";
    for (int indexed : {0, 1}) {
        Mount mount(3, 10); Q_strncpyz(mount.pack.pakFilename, zip.c_str(), sizeof(mount.pack.pakFilename));
        mount.pack.handle = unzOpen64(zip.c_str()); CHECK(mount.pack.handle);
        CHECK(unzGoToFirstFile(mount.pack.handle) == UNZ_OK);
        for (size_t i = 0; i < mount.entries.size(); ++i) {
            unz_file_info info;
            CHECK(unzGetCurrentFileInfo(mount.pack.handle, &info, mount.entries[i].name, sizeof(mount.entries[i].name), nullptr, 0, nullptr, 0) == UNZ_OK);
            mount.entries[i].len = int(info.uncompressed_size); mount.entries[i].pos = int(unzGetOffset(mount.pack.handle));
            if (i + 1 < mount.entries.size()) CHECK(unzGoToNextFile(mount.pack.handle) == UNZ_OK);
        }
        mount.link(); install(&mount.path, indexed);
        query("same.jpg", 14);
        for (qboolean unique : {qfalse, qtrue}) {
            fileHandle_t file = 0;
            CHECK(FS_FOpenFileRead("same.jpg", &file, unique) == 14 && file == 1 && fsh[file].zipFile);
            char bytes[32] = {};
            CHECK(unzReadCurrentFile(fsh[file].handleFiles.file.z, bytes, sizeof(bytes)) == 14);
            CHECK(!std::strcmp(bytes, "last duplicate"));
            unzCloseCurrentFile(fsh[file].handleFiles.file.z);
            if (unique) unzClose(fsh[file].handleFiles.file.z);
        }
        fs_pakLookupIndex.reset(); fs_searchpaths = nullptr;
        unzClose(mount.pack.handle);
    }
}
static void replacement() {
    auto old = std::unique_ptr<Mount>(new Mount(1, 10)); old->name(0, "same.jpg", 9); old->link(); install(&old->path);
    query("same.jpg", 9);
    fs_pakLookupIndex.reset(); fs_searchpaths = nullptr; old.reset(); // ASan checks stale-pointer reuse.
    Mount next(1, 30); next.name(0, "same.jpg", 18); next.link(); fs_searchpaths = &next.path;
    CHECK(query("same.jpg", 18).generation == 30);
    fs_pakLookupIndex.reset(); fs_searchpaths = nullptr;
}
static void cursorSnapshot() {
    Mount first(1, 10), second(1, 20);
    first.name(0, "same.jpg", 9); second.name(0, "same.jpg", 7); first.link(); second.link();
    first.path.next = &second.path; install(&first.path);
    auto old = fs_pakLookupIndex.lookup(fs_searchpaths, "same.jpg");
    first.path.next = nullptr; second.path.next = &first.path; fs_searchpaths = &second.path;
    fs_pakLookupIndex.invalidate();
    auto current = fs_pakLookupIndex.lookup(fs_searchpaths, "same.jpg");
    CHECK(current.next() == &second.path && current.next() == &first.path && !current.next());
    CHECK(old.next() == &first.path && old.next() == &second.path && !old.next());
    fs_pakLookupIndex.reset(); fs_searchpaths = nullptr;
}
static void benchmark() {
    const size_t count = 256, perPack = 256;
    std::vector<std::unique_ptr<Mount>> mounts;
    for (size_t pack = 0; pack < count; ++pack) {
        mounts.emplace_back(new Mount(perPack, int(pack + 1)));
        for (size_t file = 0; file < perPack; ++file) {
            char name[MAX_QPATH]; std::snprintf(name, sizeof(name), "models/pack-%03u/item-%03u.md3", unsigned(pack), unsigned(file));
            mounts.back()->name(file, name, 7);
        }
        if (pack) mounts[pack - 1]->path.next = &mounts[pack]->path;
        mounts.back()->link();
    }
    size_t comparisons[2] = {};
    for (int indexed : {0, 1}) {
        install(&mounts.front()->path, indexed); fs_numServerPaks = 1;
        filenameComparisons = pakPureChecks = 0;
        const auto started = std::chrono::steady_clock::now();
        for (int i = 0; i < 4000; ++i) {
            char name[MAX_QPATH]; std::snprintf(name, sizeof(name), "models/pack-255/item-%03u.md3", unsigned(i % 256));
            query(name, 7);
            std::snprintf(name, sizeof(name), "missing/image-%04u.png", unsigned(i)); query(name, -1);
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        comparisons[indexed] = filenameComparisons;
        std::printf("index=%d: %.2f ms, %zu legacy filename comparisons, %zu pure checks, %zu bytes, cold build %.2f ms\n", indexed, ms, filenameComparisons, pakPureChecks, fs_pakLookupIndex.memoryBytes(), indexed ? fs_pakLookupIndex.buildMilliseconds() : 0.0);
        fs_pakLookupIndex.reset(); fs_searchpaths = nullptr;
    }
    CHECK(comparisons[1] * 10 < comparisons[0]);
    fs_numServerPaks = 0;
}
}
int main(int argc, char **argv) {
    CHECK(argc == 2);
    filesystemTest::check(argv[1]);
    filesystemTest::providers(argv[1]); filesystemTest::reorder();
    filesystemTest::metadataProbes(argv[1]);
    filesystemTest::duplicateOpen(argv[1]); filesystemTest::replacement(); filesystemTest::cursorSnapshot(); filesystemTest::benchmark();
    return 0;
}
