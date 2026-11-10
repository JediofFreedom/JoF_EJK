namespace asyncTest {
enum { UI_ASYNC_PENDING, UI_ASYNC_DONE, UI_ASYNC_ERROR, UI_ASYNC_CANCELLED, UI_ASYNC_INVALID };
static std::atomic<int> scans(0);
static int FS_GetFileList(const char *, const char *, char *data, int capacity) {
    const char names[] = "kyle\0jan\0";
    CHECK(capacity >= sizeof(names));
    std::memcpy(data, names, sizeof(names));
    ++scans;
    return 2;
}
#include "async.h"

static void check() {
    int previousId = 0;
    for (int reload = 0; reload < 3; ++reload) {
        int id = uiAsyncStartGetFileList("models/players", "/", 64);
        CHECK(id > previousId); // The worker must restart after every shutdown.
        previousId = id;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        int length, status;
        while ((status = uiAsyncStatus(id, &length)) == UI_ASYNC_PENDING && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        CHECK(status == UI_ASYNC_DONE && length == 9);
        char names[64] = {};
        CHECK(uiAsyncRead(id, names, sizeof(names)) == 2);
        CHECK(!std::strcmp(names, "kyle") && !std::strcmp(names + 5, "jan"));
        uiAsyncFree(id);
        uiAsyncShutdown();
        CHECK(uiAsyncStatus(id, &length) == UI_ASYNC_INVALID);
        CHECK(uiAsyncJobs.empty() && uiAsyncQueue.empty() && !uiAsyncThread.joinable());
    }
    CHECK(scans == 3);
    int pending = uiAsyncStartGetFileList("models/players", "/", 64);
    CHECK(pending > 0);
    uiAsyncShutdown();
    CHECK(uiAsyncJobs.empty() && !uiAsyncThread.joinable());
    std::puts("UI async worker restart/cancellation checks passed.");
}
}

namespace cosmeticsTest {
#define QDECL
#define ARRAY_LEN(array) (sizeof(array) / sizeof((array)[0]))
const int MAX_QPATH = 64, MAX_COSMETIC_LENGTH = 24;
typedef int qhandle_t;
typedef struct { char name[MAX_COSMETIC_LENGTH]; qhandle_t handle; } uiCosmeticItem_t;
static struct { int totalHats, totalCapes; uiCosmeticItem_t *hats, *capes; } uiInfo;
static qboolean uiCosmeticsLoaded;
static int registrations;
static const char *UI_COSMETIC_HATS_PATH = "models/cosmetics/hats/";
static const char *UI_COSMETIC_CAPES_PATH = "models/cosmetics/capes/";
static const char *UI_COSMETIC_HATS_LEGACY_PATH = "models/players/hats/";
static const char *UI_COSMETIC_CAPES_LEGACY_PATH = "models/players/capes/";
static int fileList(const char *path, const char *, char *buffer, int size) {
    if (std::strcmp(path, UI_COSMETIC_HATS_LEGACY_PATH)) return 0;
    const char names[] = "crown.md3\0003invalid.md3\0";
    CHECK(size >= sizeof(names));
    std::memcpy(buffer, names, sizeof(names));
    return 2;
}
static int registerModel(const char *name) {
    ++registrations;
    return std::strstr(name, "crown.md3") ? 7 : 0;
}
static struct { int (*FS_GetFileList)(const char *, const char *, char *, int); int (*R_RegisterModel)(const char *); } imports = {fileList, registerModel}, *trap = &imports;
static const char *va(const char *format, const char *a, const char *b) {
    static char buffer[256];
    std::snprintf(buffer, sizeof(buffer), format, a, b);
    return buffer;
}
#include "cosmetics.h"

static void check() {
    CHECK(registrations == 0); // Startup/catalogue discovery does not load meshes.
    UI_LoadCosmetics();
    CHECK(uiCosmeticsLoaded && registrations > 0);
    CHECK(uiInfo.totalHats == ARRAY_LEN(uiKnownHats));
    CHECK(uiInfo.totalCapes == ARRAY_LEN(uiKnownCapes));
    int installed = 0;
    for (int i = 0; i < uiInfo.totalHats; ++i) {
        if (uiInfo.hats[i].handle) {
            CHECK(!std::strcmp(uiInfo.hats[i].name, "crown") && uiInfo.hats[i].handle == 7);
            ++installed;
        }
    }
    CHECK(installed == 1); // Legacy installed item, missing/cloud items, invalid name.
    int count = registrations;
    UI_LoadCosmetics();
    CHECK(registrations == count); // Reopening the menu reuses registered models.
    std::free(uiInfo.hats);
    std::free(uiInfo.capes);
    uiInfo = {};
    uiCosmeticsLoaded = qfalse;
    std::puts("UI lazy cosmetics checks passed.");
}
}

namespace pathsTest {
static struct { const char *string; } base = {"root"}, *fs_basepath = &base;
static char fs_gamedir[] = "base";
static void Com_sprintf(char *out, int size, const char *format, ...) {
    va_list args;
    va_start(args, format);
    std::vsnprintf(out, size, format, args);
    va_end(args);
}
#include "paths.h"
static void check() {
    char *normal = FS_BuildOSPath("root", "game", "model.glm");
    char *shortForm = FS_BuildOSPath("model.glm");
    std::thread scan([] {
        for (int i = 0; i < 12; ++i) {
            FS_BuildOSPath("worker", "other", "file.txt");
            FS_BuildOSPath("other.txt");
        }
    });
    scan.join();
    CHECK(!std::strcmp(normal, "root/game/model.glm"));
    CHECK(!std::strcmp(shortForm, "root/base/model.glm"));
    std::puts("Filesystem path buffers remain intact across background scans.");
}
}
