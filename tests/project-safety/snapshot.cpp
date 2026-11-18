#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <rtcapi.h>
#endif
#define CHECK(test) do { if (!(test)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); std::exit(1); } } while (0)
enum { qfalse, qtrue, BIGCHAR_WIDTH = 16, BIGCHAR_HEIGHT = 16, SMALLCHAR_WIDTH = 8,
       SMALLCHAR_HEIGHT = 8, SCREEN_WIDTH = 640, FONT_SMALL = 1, FONT_MEDIUM, FONT_SMALL2,
       CT_WHITE = 0, ITEM_TEXTSTYLE_SHADOWED = 1 };
static float colorWhite[4], colorTable[1][4];
static struct { int integer; } cg_drawSnapshot, cg_drawFPS, cg_sharpHud;
static struct Snapshot { int serverTime; } snapshot;
static struct { Snapshot *snap; int latestSnapshotNum; } cg;
static struct { int serverCommandSequence; float widthRatioCoef; } cgs = {9, 1};
static int snapshotDelta = 2, commandTimeDelta = 3;
static std::vector<std::string> drawn;
static bool asian;
static struct Services { static bool R_Language_IsAsian() { return asian; } } services, *trap = &services;
static char *va(const char *format, ...) {
    static char text[256]; va_list args; va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args); va_end(args); return text;
}
static int CG_DrawStrlen(const char *text) { return int(std::strlen(text)); }
static void CG_DrawBigString(float, float, const char *text, float) { drawn.push_back(text); }
static void CG_DrawSmallString(float, float, const char *text, float) { drawn.push_back(text); }
static void CG_DrawStringExt(float, float, const char *text, float *, int, int, float, float, int) { drawn.push_back(text); }
static float CG_Text_Width(const char *text, float, int) { return float(std::strlen(text) * 7); }
static float CG_Text_Height(const char *, float, int) { return 12; }
static void CG_Text_Paint(float, float, float, float *, const char *text, float, int, int, int) { drawn.push_back(text); }
#include "snapshot.h"
#if defined(_MSC_VER) && defined(_DEBUG)
static int __cdecl runtimeCheck(int, const char *, int, const char *, const char *, ...) {
    std::fputs("MSVC runtime check detected undefined fixture behavior.\n", stderr); std::exit(2);
}
#endif
int main() {
#if defined(_MSC_VER) && defined(_DEBUG)
    _RTC_SetErrorFunc(runtimeCheck);
#endif
    for (int disabled : {-50, -1, 0}) {
        cg_drawSnapshot.integer = disabled; cg.snap = nullptr;
        CHECK(CG_DrawSnapshot(42) == 42 && drawn.empty());
    }
    snapshot.serverTime = 123; cg.snap = &snapshot; cg.latestSnapshotNum = 7;
    for (int mode : {1, 2}) for (int font = 1; font <= 6; ++font) {
        cg_drawSnapshot.integer = mode; cg_drawFPS.integer = font; drawn.clear();
        CHECK(CG_DrawSnapshot(5) == (font <= 3 ? 25 : 17));
        CHECK(drawn.size() == 1);
        CHECK(drawn[0] == (mode == 1 ? "time:123 snap:7(2) cmd:9(3)" : "time:123 snap:7 cmd:9"));
    }
    asian = true; cg_drawFPS.integer = 1; drawn.clear();
    CHECK(CG_DrawSnapshot(5) == 17 && drawn.size() == 1);
    asian = false; cg_sharpHud.integer = 1; drawn.clear();
    CHECK(CG_DrawSnapshot(5) == 17 && drawn.size() == 1);
    return 0;
}
