#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <sstream>
#include <string>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)
typedef int qboolean;
typedef int qhandle_t;
typedef float vec4_t[4];
enum { qfalse, qtrue, TT_NUMBER = 3 };
struct rectDef_t { float x, y, w, h; };
struct cvar_t { int integer; };
static cvar_t ui_currentNetMap, ui_currentMap;
static struct {
    int mapCount;
    struct { int levelShot; const char *imageName; } mapList[2];
    struct { float widthRatioCoef; } uiDC;
    struct { int currentServerPreview; } serverStatus;
} uiInfo;
static std::vector<std::string> shaderRequests;
static int standardShader = 72, wideShader = 71, imageShader;
static qhandle_t drawn;
static rectDef_t drawnRect;
static int cvarUpdates;
static void UI_DrawHandlePic(float x, float y, float w, float h, qhandle_t shader) {
    drawn = shader; drawnRect = {x, y, w, h};
}
static struct {
    static int R_RegisterShaderNoMip(const char *name) {
        CHECK(name); shaderRequests.push_back(name);
        if (!std::strcmp(name, "menu/art/unknownmap_mp_16_9")) return wideShader;
        if (!std::strcmp(name, "menu/art/unknownmap_mp")) return standardShader;
        CHECK(!std::strcmp(name, "map0") || !std::strcmp(name, "map1")); return imageShader;
    }
    static void Cvar_Set(const char *name, const char *value) {
        CHECK(!std::strcmp(value, "0"));
        if (!std::strcmp(name, "ui_currentNetMap")) ui_currentNetMap.integer = 0;
        else { CHECK(!std::strcmp(name, "ui_currentMap")); ui_currentMap.integer = 0; }
    }
    static void Cvar_Update(cvar_t *) { ++cvarUpdates; }
} previewServices;
static decltype(previewServices) *trap = &previewServices;
#include "previews.h"

namespace parser {
#include "edit_type.h"
struct itemDef_t { const char *cvar; struct { editFieldDef_t *edit; } typeData; };
struct pc_token_t { int type, intvalue; char string[256]; };
static std::vector<pc_token_t> tokens;
static std::deque<std::string> strings;
static size_t nextToken;
static int errors;
static void Item_ValidateTypeData(itemDef_t *) {}
static int Q_stricmp(const char *a, const char *b) {
    for (; *a && *b; ++a, ++b) {
        const int diff = std::tolower(static_cast<unsigned char>(*a)) - std::tolower(static_cast<unsigned char>(*b));
        if (diff) return diff;
    }
    return int(static_cast<unsigned char>(*a)) - static_cast<unsigned char>(*b);
}
static const char *String_Alloc(const char *text) { strings.emplace_back(text); return strings.back().c_str(); }
static void PC_SourceError(int, const char *, ...) { ++errors; }
static struct {
    static int PC_ReadToken(int, pc_token_t *out) {
        if (nextToken == tokens.size()) return 0; *out = tokens[nextToken++]; return 1;
    }
} services;
static decltype(services) *trap = &services;
#include "item.h"
static void input(const char *text) {
    tokens.clear(); strings.clear(); nextToken = 0; errors = 0;
    std::istringstream source(text); std::string word;
    while (source >> word) {
        if (word.size() > 1 && word[0] == '-') {
            pc_token_t minus = {}; std::strcpy(minus.string, "-"); tokens.push_back(minus); word.erase(0, 1);
        }
        pc_token_t token = {}; std::snprintf(token.string, sizeof(token.string), "%s", word.c_str());
        char *end; const long number = std::strtol(word.c_str(), &end, 10);
        if (*word.c_str() && !*end) { token.type = TT_NUMBER; token.intvalue = int(number); }
        tokens.push_back(token);
    }
}
static void check() {
    editFieldDef_t edit = {};
    itemDef_t item = {nullptr, {&edit}};
    input("fov 90 1 179"); CHECK(ItemParse_cvarInt(&item, 1));
    CHECK(!std::strcmp(item.cvar, "fov") && edit.defVal == 90 && edit.minVal == 1 && edit.maxVal == 179);
    input("offset -5 -10 5"); CHECK(ItemParse_cvarInt(&item, 1));
    CHECK(edit.defVal == -5 && edit.minVal == -10 && edit.maxVal == 5);
    for (const char *malformed : {"", "fov", "fov 90", "fov 90 1", "fov bad 1 179", "fov 90 bad 179", "fov 90 1 bad", "fov -"}) {
        input(malformed); edit.defVal = 101; edit.minVal = 202; edit.maxVal = 303;
        CHECK(!ItemParse_cvarInt(&item, 1));
        CHECK(edit.defVal == 101 && edit.minVal == 202 && edit.maxVal == 303);
    }
    input("fov 90 1 179"); item.typeData.edit = nullptr;
    CHECK(!ItemParse_cvarInt(&item, 1) && nextToken == 0);
}
}
static void reset() {
    uiInfo = {}; uiInfo.mapCount = 2; uiInfo.mapList[0].imageName = "map0"; uiInfo.mapList[1].imageName = "map1";
    ui_currentMap = {}; ui_currentNetMap = {}; uiInfo.uiDC.widthRatioCoef = 1;
    shaderRequests.clear(); standardShader = 72; wideShader = 71; imageShader = 0; drawn = -1; cvarUpdates = 0;
}
namespace menuParser {
enum { MAX_MENUITEMS = 4, MEM_POOL_SIZE = 256 };
struct itemDef_t { void *parent; float textscale; struct { int initialized; } window; };
struct menuDef_t { int itemCount; itemDef_t *items[MAX_MENUITEMS]; };
alignas(16) static char memoryPool[MEM_POOL_SIZE];
static int allocPoint;
static qboolean outOfMemory;
static int initialized, controls, applied, parsed;
static bool validItem = true;
static void print(const char *, ...) {}
static struct { void (*Print)(const char *, ...); } services = {print};
static decltype(services) *DC = &services;
static void Window_Init(decltype(itemDef_t::window) *window) { ++initialized; window->initialized = 1; }
static qboolean Item_Parse(int, itemDef_t *) { ++parsed; return validItem; }
static void Item_InitControls(itemDef_t *) { ++controls; }
static void Item_ApplyHacks(itemDef_t *) { ++applied; }
#include "menu.h"
static void check() {
    menuDef_t menu = {};
    CHECK(MenuParse_itemDef(reinterpret_cast<itemDef_t *>(&menu), 1));
    CHECK(menu.itemCount == 1 && menu.items[0] && menu.items[0]->parent == &menu);
    CHECK(menu.items[0]->textscale == 0.55f && menu.items[0]->window.initialized == 1);
    CHECK(initialized == 1 && controls == 1 && applied == 1 && parsed == 1);
    // Exercise the actual arena's documented allocation failure. The pending
    // menu slot and item count must remain intact and initialization must stop.
    allocPoint = MEM_POOL_SIZE; itemDef_t sentinel = {};
    menu.items[1] = &sentinel;
    CHECK(!MenuParse_itemDef(reinterpret_cast<itemDef_t *>(&menu), 1));
    CHECK(outOfMemory && menu.itemCount == 1 && menu.items[1] == &sentinel);
    CHECK(initialized == 1 && controls == 1 && applied == 1 && parsed == 1);
    allocPoint = 0; validItem = false;
    CHECK(!MenuParse_itemDef(reinterpret_cast<itemDef_t *>(&menu), 1));
    CHECK(menu.itemCount == 1 && controls == 1 && applied == 1 && parsed == 2);
    menu.itemCount = MAX_MENUITEMS; const int oldPoint = allocPoint;
    CHECK(MenuParse_itemDef(reinterpret_cast<itemDef_t *>(&menu), 1));
    CHECK(menu.itemCount == MAX_MENUITEMS && allocPoint == oldPoint);
}
}
static void drawMap(bool net) {
    rectDef_t rect = {10,20,320,180}; vec4_t color = {};
    UI_DrawMapPreview(&rect, 1, color, net);
    CHECK(drawnRect.x == rect.x && drawnRect.y == rect.y && drawnRect.w == rect.w && drawnRect.h == rect.h);
}
static void mapPreviews() {
    for (bool net : {false, true}) {
        reset(); uiInfo.mapList[0].levelShot = 99; drawMap(net); CHECK(drawn == 99 && shaderRequests.empty());
        // Switching from an existing map image to a missing one must never
        // reuse a value left in the previous invocation's stack frame.
        uiInfo.mapList[0].levelShot = 0; drawMap(net); CHECK(drawn == 72);
        CHECK(shaderRequests == std::vector<std::string>{"menu/art/unknownmap_mp"});
        reset(); uiInfo.mapList[0].levelShot = -1; imageShader = 88;
        drawMap(net); CHECK(drawn == 88 && uiInfo.mapList[0].levelShot == 88);
        drawMap(net); CHECK(shaderRequests.size() == 1);
        reset(); uiInfo.mapList[0].levelShot = -1; drawMap(net); CHECK(drawn == 72 && shaderRequests.size() == 2);
        for (float ratio : {0.74f, 0.75f, 0.76f}) {
            reset(); uiInfo.uiDC.widthRatioCoef = ratio; drawMap(net); CHECK(drawn == 71);
            wideShader = 0; drawMap(net); CHECK(drawn == 72);
        }
        reset(); if (net) ui_currentNetMap.integer = 2; else ui_currentMap.integer = 2;
        uiInfo.mapList[0].levelShot = 88; drawMap(net); CHECK(drawn == 88 && cvarUpdates == 1);
        reset(); uiInfo.mapCount = 0; uiInfo.mapList[0].imageName = nullptr;
        drawMap(net); CHECK(drawn == 72 && shaderRequests.size() == 1);
    }
}
static void netPreviews() {
    rectDef_t rect = {10,20,320,180}; vec4_t color = {};
    reset(); uiInfo.serverStatus.currentServerPreview = 99;
    UI_DrawNetMapPreview(&rect, 1, color); CHECK(drawn == 99 && shaderRequests.empty());
    uiInfo.serverStatus.currentServerPreview = 0;
    UI_DrawNetMapPreview(&rect, 1, color); CHECK(drawn == 72);
    reset(); uiInfo.uiDC.widthRatioCoef = 0.75f;
    UI_DrawNetMapPreview(&rect, 1, color); CHECK(drawn == 71);
    wideShader = 0; UI_DrawNetMapPreview(&rect, 1, color); CHECK(drawn == 72);
}
int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!std::strcmp(argv[1], "preview-map")) mapPreviews();
    else if (!std::strcmp(argv[1], "preview-net")) netPreviews();
    else if (!std::strcmp(argv[1], "cvar-int")) parser::check();
    else { CHECK(!std::strcmp(argv[1], "menu-allocation")); menuParser::check(); }
    std::puts("UI preview/parser regressions passed.");
}
