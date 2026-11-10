namespace headsTest {
const int MAX_QPATH = 64, PROFILE_SKIN_SIZE = 8192, MAX_Q3PLAYERMODELS = 16, FS_READ = 0;
static struct {
    int q3HeadCount;
    char q3HeadNames[MAX_Q3PLAYERMODELS][MAX_QPATH];
    int q3HeadIcons[MAX_Q3PLAYERMODELS];
    struct { struct { int defaultIconRed, defaultIconBlue, defaultIconRGB, defaultIcon; } Assets; } uiDC;
} uiInfo;
static struct { int integer; } ui_showAllSkins = {2}, ui_sv_pure = {0};
static int shaderRegistrations;
static int open(const char *name, int *file, int) {
    bool exists = std::strstr(name, ".skin") ||
        (std::strstr(name, "/image/icon_") && std::strstr(name, ".png"));
    *file = exists ? 1 : 0;
    return exists ? 1 : -1;
}
static void close(int) {}
static int shader(const char *name) {
    ++shaderRegistrations;
    return std::strstr(name, "/script/icon_") ? 42 : 0;
}
static struct { int (*FS_Open)(const char *, int *, int); void (*FS_Close)(int); int (*R_RegisterShaderNoMip)(const char *); } imports = {open, close, shader}, *trap = &imports;
static void Com_sprintf(char *out, int size, const char *format, ...) {
    va_list args;
    va_start(args, format);
    std::vsnprintf(out, size, format, args);
    va_end(args);
}
static const char *va(const char *format, const char *a, const char *b) {
    static char buffer[256];
    std::snprintf(buffer, sizeof(buffer), format, a, b);
    return buffer;
}
#include "heads.h"

static void check() {
    uiInfo.uiDC.Assets = {11, 12, 13, 14};
    char files[PROFILE_SKIN_SIZE] = "model_default.skin";
    char image[] = "image", script[] = "script", missing[] = "missing";
    UI_BuildQ3Model_List_ProcessDir(image, files, 1);
    CHECK(uiInfo.q3HeadCount == 1 && !std::strcmp(uiInfo.q3HeadNames[0], "image/default"));
    CHECK(uiInfo.q3HeadIcons[0] == 0 && shaderRegistrations == 0); // Feeder resolves on display.
    UI_BuildQ3Model_List_ProcessDir(image, files, 1);
    CHECK(uiInfo.q3HeadCount == 1 && shaderRegistrations == 0); // Duplicate metadata.
    UI_BuildQ3Model_List_ProcessDir(script, files, 1);
    CHECK(uiInfo.q3HeadCount == 2 && uiInfo.q3HeadIcons[1] == 42); // Shader-only icon still works.
    UI_BuildQ3Model_List_ProcessDir(missing, files, 1);
    CHECK(uiInfo.q3HeadCount == 3 && uiInfo.q3HeadIcons[2] == 14); // Existing missing-icon fallback.
    CHECK(shaderRegistrations == 2);
    std::puts("Model catalogue defers image uploads and preserves shader/fallback icons.");
}
}
