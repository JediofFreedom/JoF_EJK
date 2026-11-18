#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <minizip/unzip.h>
#include "filesystem_archive_identity.h"

// Checks must also run in Release builds.
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); std::exit(1); } } while (0)
typedef unsigned char byte;
typedef int qboolean;
enum { qfalse, qtrue, TAG_FILESYS = 1, ERR_FATAL = 1 };
const int MAX_OSPATH = 256, MAX_ZPATH = 256, MAX_FILEHASH_SIZE = 1024;
const char PATH_SEP = '/';
#define MAX_TOKEN_CHARS 1024
#define MAX_SHADERTEXT_HASH 2048
#define Com_Memset std::memset
#define Com_Memcpy std::memcpy
static int LittleLong(unsigned int value) { return static_cast<int>(value); }
static void Com_Error(int, const char *message) { throw std::runtime_error(message); }
static int liveAllocations;
static void *Z_Malloc(size_t size, int, qboolean = qfalse) {
    void *memory = std::calloc(1, size ? size : 1);
    CHECK(memory);
    ++liveAllocations;
    return memory;
}
static void Z_Free(void *memory) { if (memory) --liveAllocations; std::free(memory); }
extern "C" void *openjk_minizip_malloc(int size) { return Z_Malloc(size, TAG_FILESYS); }
extern "C" int openjk_minizip_free(void *memory) { Z_Free(memory); return 0; }
#include "engine_strings.h"
#include "checksum.h"
static char *CopyString(const char *name) {
    char *copy = static_cast<char *>(Z_Malloc(std::strlen(name) + 1, TAG_FILESYS));
    std::strcpy(copy, name);
    return copy;
}

static char com_token[MAX_TOKEN_CHARS];
static int com_lines, com_tokenline;
static size_t parsedTokens;
static char *RealCOM_ParseExt(const char **, qboolean);
static char *COM_ParseExt(const char **text, qboolean lines) {
    ++parsedTokens;
    return RealCOM_ParseExt(text, lines);
}
#include "parser.h"

namespace vanilla {
static char *s_shaderText;
static char **shaderTextHashTable[MAX_SHADERTEXT_HASH];
#include "vanilla.h"
}
namespace rend2 {
static char *s_shaderText;
static char **shaderTextHashTable[MAX_SHADERTEXT_HASH];
#include "rend2.h"
}
namespace dedicated {
static char *s_shaderText;
static char **shaderTextHashTable[MAX_SHADERTEXT_HASH];
#include "dedicated.h"
}
#ifdef EXPECT_OPTIMIZED
namespace vulkan {
static char *s_shaderText;
static char **shaderTextHashTable[MAX_SHADERTEXT_HASH];
#include "vulkan.h"
}
#endif

#include "types.h"
static searchpath_t *fs_searchpaths;
static int fs_numServerPaks, fs_checksumFeed = 0x12345678;
static std::mutex fs_listMutex;
static pack_t *excludedPak;
static struct { int integer; } zipCatalogSetting = {1};
static auto *fs_zipCatalogFast = &zipCatalogSetting;
static qboolean FS_PakIsPure(pack_t *pack) { return pack != excludedPak; }
static std::vector<std::string> looseFiles;
static char *FS_BuildOSPath(const char *, const char *, const char *) { static char path[] = "fixture"; return path; }
static char **Sys_ListFiles(const char *, const char *, char *, int *count, qboolean) {
    *count = static_cast<int>(looseFiles.size());
    char **result = static_cast<char **>(Z_Malloc((*count + 1) * sizeof(char *), TAG_FILESYS));
    for (int i = 0; i < *count; ++i) result[i] = CopyString(looseFiles[i].c_str());
    return result;
}
static void Sys_FreeFileList(char **list) {
    if (!list) return;
    for (char **p = list; *p; ++p) Z_Free(*p);
    Z_Free(list);
}
// The glob matcher is an engine service, not part of this change.
static qboolean Com_FilterPath(const char *filter, const char *name, qboolean) {
    return !std::strcmp(filter, "*") || !Q_stricmp(filter, name);
}
#include "filesystem.h"
#include "ui_checks.h"
#include "head_checks.h"

using Clock = std::chrono::steady_clock;
static double milliseconds(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

static void checkShaders(const char *renderer, char *&text, char ***table,
                         long (*hash)(const char *, int), const char *(*find)(const char *), void (*kill)()) {
    std::string scripts = "// overrides first\nTextures/Duplicate { override }\ntextures/duplicate { normal }\n";
    for (int i = 0; i < 12000; ++i)
        scripts += "textures/test" + std::to_string(i) + " { { map textures/diffuse } }\n";
    text = &scripts[0];
    std::vector<char *> buckets[MAX_SHADERTEXT_HASH];
    const char *cursor = text;
    while (cursor && *cursor) {
        char *entry = const_cast<char *>(cursor);
        char *name = COM_ParseExt(&cursor, qtrue);
        if (!*name) break;
        buckets[hash(name, MAX_SHADERTEXT_HASH)].push_back(entry);
        CHECK(SkipBracedSection(&cursor, 0));
    }
    for (int i = 0; i < MAX_SHADERTEXT_HASH; ++i) {
        buckets[i].push_back(nullptr);
        table[i] = buckets[i].data();
    }
    const char *definition = find("textures/DUPLICATE");
    CHECK(definition && std::strstr(definition, "override") < std::strstr(definition, "normal"));
    CHECK(find("textures/test11999"));
    // An allocated empty bucket is also an authoritative miss.
    const int emptyHash = static_cast<int>(hash("textures/empty-bucket", MAX_SHADERTEXT_HASH));
    char **savedBucket = table[emptyHash];
    char *emptyBucket[] = {nullptr};
    table[emptyHash] = emptyBucket;
    parsedTokens = 0;
    CHECK(!find("textures/empty-bucket"));
#ifdef EXPECT_OPTIMIZED
    CHECK(parsedTokens == 0);
#endif
    table[emptyHash] = savedBucket;
    parsedTokens = 0;
    auto start = Clock::now();
    for (int i = 0; i < 400; ++i) {
        std::string name = "textures/implicit" + std::to_string(i);
        CHECK(find(name.c_str()) == nullptr);
    }
    std::printf("%s: 400 misses in %.3f ms, %zu parsed tokens\n", renderer, milliseconds(start), parsedTokens);
#ifdef EXPECT_OPTIMIZED
    CHECK(parsedTokens < 10000); // Full-library fallbacks would parse >30 million tokens.
#endif
    kill();
    CHECK(find("textures/duplicate")); // Preserve lookup while the index is unavailable.
    text = nullptr;
    CHECK(!find("anything"));
}

static void checkPack(pack_t *pack) {
    CHECK(pack);
    unzFile archive = unzOpen(pack->pakFilename);
    CHECK(archive);
    unz_global_info global;
    CHECK(unzGetGlobalInfo(archive, &global) == UNZ_OK);
    CHECK(pack->numfiles == static_cast<int>(global.number_entry));
    std::vector<int> crcs(1, fs_checksumFeed);
    int status = unzGoToFirstFile(archive);
    for (int i = 0; i < pack->numfiles; ++i) {
        CHECK(status == UNZ_OK);
        char name[MAX_ZPATH];
        unz_file_info info;
        CHECK(unzGetCurrentFileInfo(archive, &info, name, sizeof(name), nullptr, 0, nullptr, 0) == UNZ_OK);
        const fileInPack_t &entry = pack->buildBuffer[i];
        CHECK(!std::strcmp(entry.name, name));
        CHECK(entry.pos == unzGetOffset(archive));
        CHECK(entry.len == info.uncompressed_size);
        if (info.uncompressed_size) crcs.push_back(LittleLong(info.crc));
        status = unzGoToNextFile(archive);
    }
    CHECK(pack->checksum == LittleLong(Com_BlockChecksum(crcs.data() + 1, (crcs.size() - 1) * sizeof(int))));
    CHECK(pack->pure_checksum == LittleLong(Com_BlockChecksum(crcs.data(), crcs.size() * sizeof(int))));
#ifdef EXPECT_OPTIMIZED
    std::vector<bool> indexed(pack->numfiles);
    for (int i = 0; i < pack->numfiles; ++i) {
        int index = pack->nameIndex[i];
        CHECK(index >= 0 && index < pack->numfiles && !indexed[index]);
        indexed[index] = true;
        if (i) CHECK(Q_stricmp(pack->buildBuffer[pack->nameIndex[i - 1]].name, pack->buildBuffer[index].name) <= 0);
    }
#endif
    // A stored offset must still open the same compressed bytes and pass its CRC.
    for (int i = 0; i < pack->numfiles; ++i) {
        if (!pack->buildBuffer[i].len) continue;
        CHECK(unzSetOffset(archive, pack->buildBuffer[i].pos) == UNZ_OK);
        CHECK(unzOpenCurrentFile(archive) == UNZ_OK);
        char data[16384];
        unsigned long read = 0;
        int bytes;
        while ((bytes = unzReadCurrentFile(archive, data, sizeof(data))) > 0) read += bytes;
        CHECK(bytes == 0 && read == pack->buildBuffer[i].len);
        CHECK(unzCloseCurrentFile(archive) == UNZ_OK);
        break;
    }
    unzClose(archive);
}

static std::vector<std::string> listFiles(const char *path, const char *extension, char *filter = nullptr) {
    int count;
    char **list = FS_ListFilteredFiles(path, extension, filter, &count);
    std::vector<std::string> result;
    for (int i = 0; i < count; ++i) result.push_back(list[i]);
    CHECK(!list || !list[count]);
    Sys_FreeFileList(list);
    return result;
}

static void checkListing(pack_t *pack) {
    directory_t directory = {};
    searchpath_t loose = {nullptr, nullptr, &directory};
    searchpath_t lower = {&loose, pack, nullptr};
    searchpath_t higher = {&lower, pack, nullptr};
    fs_searchpaths = &higher;
    looseFiles = {"OVERRIDE.shader", "loose.shader"};
    auto shaders = listFiles("shaders", ".shader");
    CHECK((shaders == std::vector<std::string>{"Override.shader", "loose.shader"}));
    CHECK(listFiles("shaders", ".shader") == shaders); // State is local to each call.
    excludedPak = pack;
    CHECK((listFiles("shaders", ".shader") == looseFiles));
    excludedPak = nullptr;
    fs_numServerPaks = 1;
    CHECK((listFiles("shaders/", ".shader") == std::vector<std::string>{"Override.shader"}));
    fs_numServerPaks = 0;
    looseFiles.clear();
    CHECK(listFiles("no-match", ".shader").empty());
    CHECK(listFiles(nullptr, nullptr).empty());
    CHECK(listFiles("", ".shader").size() == 1);
    char filter[] = "*";
    CHECK(listFiles("", "", filter).size() == MAX_FOUND_FILES - 1);
    CHECK(listFiles("models/players", ".skin").size() == MAX_FOUND_FILES - 1);
    // Preserve the legacy prefix match, including similarly named directories.
    CHECK((listFiles("TEXTURES/zeta", ".tga") == std::vector<std::string>{"second.tga", "first.tga", "l/extra.tga"}));
    CHECK((listFiles("textures/zeta/", ".tga") == std::vector<std::string>{"second.tga", "first.tga", "deep/more/hidden.tga", "l/extra.tga"}));
    CHECK((listFiles("textures", ".tga") == std::vector<std::string>{"zeta/second.tga", "Alpha/first.tga", "zeta/first.tga", "zetail/extra.tga"}));
    auto start = Clock::now();
    for (int i = 0; i < 8; ++i)
        CHECK(listFiles("models/players", ".skin").size() == MAX_FOUND_FILES - 1);
    std::printf("filesystem: 8 listings of 6000 names across 2 packages in %.3f ms\n", milliseconds(start));
    start = Clock::now();
    for (int i = 0; i < 120; ++i) {
        char path[64];
        std::snprintf(path, sizeof(path), "models/players/test%05d", i);
        CHECK((listFiles(path, ".skin") == std::vector<std::string>{"model.skin"}));
    }
    std::printf("model directories: 120 listings in %.3f ms\n", milliseconds(start));
    fs_searchpaths = nullptr;
}

int main(int argc, char **argv) {
    CHECK(argc > 1);
    checkShaders("vanilla", vanilla::s_shaderText, vanilla::shaderTextHashTable,
        vanilla::generateHashValue, vanilla::FindShaderInShaderText, vanilla::KillTheShaderHashTable);
    checkShaders("rend2", rend2::s_shaderText, rend2::shaderTextHashTable,
        rend2::generateHashValue, rend2::FindShaderInShaderText, rend2::KillTheShaderHashTable);
    checkShaders("dedicated", dedicated::s_shaderText, dedicated::shaderTextHashTable,
        dedicated::generateHashValue, dedicated::FindShaderInShaderText, dedicated::KillTheShaderHashTable);
#ifdef EXPECT_OPTIMIZED
    checkShaders("vulkan", vulkan::s_shaderText, vulkan::shaderTextHashTable,
        vulkan::generateHashValue, vulkan::FindShaderInShaderText, vulkan::KillTheShaderHashTable);
#endif
    CHECK(!FS_LoadZipFile("missing-file.pk3", "missing"));
    for (int i = 1; i < argc; ++i) {
        auto start = Clock::now();
        pack_t *pack = FS_LoadZipFile(argv[i], "fixture.pk3");
        double elapsed = milliseconds(start);
        checkPack(pack);
        std::printf("mount: %s: %d entries, %.3f ms, checksums %08x/%08x\n",
            argv[i], pack->numfiles, elapsed, pack->checksum, pack->pure_checksum);
        if (i == 1) checkListing(pack);
        FS_FreePak(pack);
    }
#ifdef EXPECT_OPTIMIZED
    asyncTest::check();
    cosmeticsTest::check();
    pathsTest::check();
    headsTest::check();
    std::string parent = argv[1];
    parent = parent.substr(0, parent.find_last_of("/\\") + 1);
    CHECK(!FS_LoadZipFile((parent + "long-name.pk3").c_str(), "invalid"));
    CHECK(!FS_LoadZipFile((parent + "damaged.pk3").c_str(), "invalid"));
#endif
    CHECK(liveAllocations == 0);
    std::puts("Loading checks passed.");
}
