// Compile the real resolver, including its pure/loose and private-key rules.
#include "filesystem_pak_index.h"
namespace filesystemTest {
typedef int fileHandle_t;
enum fsMode_t { FS_READ, FS_WRITE, FS_APPEND, FS_APPEND_SYNC };
enum { FS_GENERAL_REF = 1, FS_CGAME_REF = 2, FS_UI_REF = 4, ERR_FATAL, CA_ACTIVE, KEYCATCH_UI };
const int MAX_FILEHASH_SIZE = 1024;
const char PATH_SEP = '/';
#define Q3CONFIG_AUTO "autoexec.cfg"
#define Q3CONFIG_CFG "eternaljk.cfg"
struct Entry { char name[MAX_QPATH]; int len, pos; Entry *next; };
struct Pack { int hashSize; Entry **hashTable; int referenced; char pakFilename[256]; unzFile handle; bool pure; uint64_t sourceIdentity; int checksum; };
struct Directory { char path[512], gamedir[2]; };
struct Search { Search *next; Pack *pack; Directory *dir; };
typedef Search searchpath_t;
typedef Pack pack_t;
typedef Entry fileInPack_t;
typedef Directory directory_t;
static Search *fs_searchpaths;
static PakProviderIndex<searchpath_t> fs_pakLookupIndex;
static struct { int integer; } lookupSetting = {1}, *fs_lookupIndex = &lookupSetting;
static int Cvar_VariableIntegerValue(const char *) { return 0; }
static std::mutex fs_listMutex;
static int handleAllocations, fs_numServerPaks, fs_fakeChkSum;
static size_t pakPureChecks, filenameComparisons;
static bool com_fullyInitialized = true;
static struct { int integer; } debug, *fs_debug = &debug;
static struct Handle {
    struct { struct { FILE *o; unzFile z; } file; int unique; } handleFiles;
    char name[MAX_QPATH]; int zipFile, zipFilePos, zipFileLen, fileSize, handleSync, handleAsync;
} fsh[8];
static void FS_AssertInitialised() {}
static void Com_Error(int, const char *, ...) { throw 1; }
static void Com_DPrintf(const char *, ...) {}
static int FS_HandleForFile() { ++handleAllocations; return 1; }
#ifdef EJK_PROVIDER_PRODUCTION_HASH
#include "filesystem_hash.h"
#include "filesystem_compare.h"
#else
static long FS_HashFileName(const char *, int) { return 0; } // Single bucket fixture.
#endif
static bool FS_PakIsPure(Pack *pack) { ++pakPureChecks; return !fs_numServerPaks || pack->pure; }
static int FS_FilenameCompare(const char *a, const char *b) {
    ++filenameComparisons;
#ifdef EJK_PROVIDER_PRODUCTION_HASH
    return FS_ProviderFixtureCompare(a, b);
#else
    std::string x(a), y(b); std::replace(x.begin(), x.end(), '\\', '/'); std::replace(y.begin(), y.end(), '\\', '/');
    return Q_stricmp(x.c_str(), y.c_str());
#endif
}
static bool FS_IsExt(const char *name, const char *ext, int length) {
    int n = int(std::strlen(ext)); return length >= n && !Q_stricmp(name + length - n, ext);
}
static bool FS_IsDemoExt(const char *name, int length) { return FS_IsExt(name, ".dm_26", length); }
static float Q_flrand(float, float) { return 0; }
static char *FS_BuildOSPath(const char *base, const char *, const char *path) {
    static char full[1024]; std::snprintf(full, sizeof(full), "%s/%s", base, path); return full;
}
static long FS_fplength(FILE *file) { std::fseek(file, 0, SEEK_END); return std::ftell(file); }
static int FS_FOpenFileWrite(const char *) { return 0; }
static int FS_FOpenFileAppend(const char *) { return 0; }
// The platform-specific network-drive copy policy is irrelevant to queries.
#ifdef _WIN32
#define AUDIT_WINDOWS
#undef _WIN32
#endif
#define DEDICATED
#include "filesystem.h"
#undef DEDICATED
#ifdef AUDIT_WINDOWS
#define _WIN32
#undef AUDIT_WINDOWS
#endif
static void checkMode(const char *root, int indexed) {
    lookupSetting.integer = indexed;
    fs_pakLookupIndex.reset();
    Entry a = {"probe.jpg", 9, 0, nullptr}, b = {"probe.jpg", 7, 0, nullptr};
    Entry *ah[] = {&a}, *bh[] = {&b};
    Pack high = {1, ah, 0, {}, nullptr, true, 42, 123}, low = {1, bh, 0, {}, nullptr, true, 43, 456};
    Directory directory = {}; Q_strncpyz(directory.path, root, sizeof(directory.path));
    Search tail = {nullptr, &low, nullptr}, middle = {&tail, &high, nullptr}, head = {&middle, nullptr, &directory};
    fs_searchpaths = &head;
    FILE *loose = std::fopen((std::string(root) + "/probe.jpg").c_str(), "wb"); CHECK(loose);
    std::fputs("loose override", loose); std::fclose(loose);
    CHECK(FS_FOpenFileByMode("probe.jpg", nullptr, FS_READ) == 14);
    fs_numServerPaks = 1;
	fsReadFileIdentity_t id = {};
	CHECK(FS_GetReadFileIdentity("probe.jpg", &id, qfalse) && id.provider == 1 && id.size == 9);
	const auto mounted = id;
	++high.sourceIdentity;
	CHECK(FS_GetReadFileIdentity("probe.jpg", &id, qfalse) && id.generation != mounted.generation);
    CHECK(FS_FOpenFileByMode("probe.jpg", nullptr, FS_READ) == 9);
    high.pure = false; CHECK(FS_FOpenFileByMode("/probe.jpg", nullptr, FS_READ) == 7);
    low.pure = false; CHECK(FS_FOpenFileByMode("probe.jpg", nullptr, FS_READ) == -1);
    fs_numServerPaks = 0;
	CHECK(FS_GetReadFileIdentity("probe.jpg", &id, qfalse) && id.provider == 2);
	CHECK(!FS_GetReadFileIdentity("anywhere/identity.key", &id, qfalse));
    CHECK(FS_FOpenFileByMode("../probe.jpg", nullptr, FS_READ) == -1);
    CHECK(FS_FOpenFileByMode("q3key", nullptr, FS_READ) == -1);
    CHECK(FS_FOpenFileByMode("anywhere/identity.key", nullptr, FS_READ) == -1);
    CHECK(FS_FOpenFileByMode("probe.jpg", nullptr, FS_WRITE) == -1);
    CHECK(FS_FOpenFileByMode("autoexec.cfg", nullptr, FS_READ) == -1);
    FILE *empty = std::fopen((std::string(root) + "/empty.png").c_str(), "wb"); CHECK(empty); std::fclose(empty);
    CHECK(FS_FOpenFileByMode("empty.png", nullptr, FS_READ) == 0);
    CHECK(!std::remove((std::string(root) + "/empty.png").c_str()));
    CHECK(handleAllocations == 0 && high.referenced == 0 && low.referenced == 0);
	fs_numServerPaks = 1; high.pure = true;
	CHECK(FS_GetReadFileIdentity("probe.jpg", &id, qtrue) && high.referenced == FS_GENERAL_REF);
	CHECK(handleAllocations == 0);
	fs_numServerPaks = 0;
    CHECK(!std::remove((std::string(root) + "/probe.jpg").c_str()));
    fs_pakLookupIndex.reset();
    fs_searchpaths = nullptr;
}
static void check(const char *root) {
    checkMode(root, 0);
    checkMode(root, 1);
}
}
