#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)
#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))
typedef unsigned char byte;
typedef int qboolean;
typedef int qhandle_t;
enum { qfalse, qtrue, h_low, PRINT_WARNING, PRINT_ALL };
const int MAX_QPATH = 64, MAX_SKINS = 16, MAX_TOKEN_CHARS = 1024;
static const int lightmapsNone[4] = {-1,-1,-1,-1};
static const byte stylesDefault[4] = {};
struct shader_t { char name[MAX_QPATH]; };
struct skinSurface_t { char name[MAX_QPATH]; shader_t *shader; };
struct skin_s { char name[MAX_QPATH]; int numSurfaces; skinSurface_t *surfaces[32]; };
typedef skin_s skin_t;
static struct { int numSkins; skin_t *skins[MAX_SKINS]; } tr;
static bool gServerSkinHack;
static std::vector<void *> allocations;
static std::map<std::string, std::string> files;
static std::map<std::string, shader_t> shaders;
static int fileReads;
static std::string lower(std::string value) {
    for (char &c : value) c = char(std::tolower(static_cast<unsigned char>(c))); return value;
}
static void Q_strncpyz(char *out, const char *in, size_t count) {
    CHECK(count && std::strlen(in) < count); std::memcpy(out, in, std::strlen(in) + 1);
}
static void Q_strcat(char *out, size_t size, const char *in) {
    CHECK(std::strlen(out) + std::strlen(in) < size); std::strcat(out, in);
}
static void Q_strlwr(char *value) { for (; *value; ++value) *value = char(std::tolower(static_cast<unsigned char>(*value))); }
static int Q_stricmp(const char *a, const char *b) { return lower(a).compare(lower(b)); }
static void *Hunk_Alloc(size_t size, int) {
    void *data = std::calloc(1, size); CHECK(data); allocations.push_back(data); return data;
}
static void R_IssuePendingRenderCommands() {}
static void Com_Printf(const char *, ...) {}
static void vk_debug(const char *, ...) {}
static shader_t *R_FindShader(const char *name, const int *, const byte *, qboolean) {
    auto &shader = shaders[name]; Q_strncpyz(shader.name, name, sizeof(shader.name)); return &shader;
}
static shader_t *R_FindServerShader(const char *name, const int *lightmap, const byte *styles, qboolean raw) {
    return R_FindShader(name, lightmap, styles, raw);
}
static struct {
    static long FS_ReadFile(const char *name, void **out) {
        ++fileReads; const auto found = files.find(lower(name));
        if (found == files.end()) { *out = nullptr; return -1; }
        *out = std::malloc(found->second.size() + 1); CHECK(*out);
        std::memcpy(*out, found->second.c_str(), found->second.size() + 1); return long(found->second.size());
    }
    static void FS_FreeFile(void *data) { std::free(data); }
    static void Printf(int, const char *, ...) {}
} ri;
#include SKIN_SOURCE

static void reset() {
    for (auto *data : allocations) std::free(data);
    allocations.clear(); files.clear(); shaders.clear();
    tr = {}; tr.numSkins = 1; fileReads = 0;
}
static void registration() {
    reset(); CHECK(!RE_RegisterSkin(nullptr) && !RE_RegisterSkin(""));
    for (const char *name : {"a", "ab", "abc", "abcd", "normal.skin"}) {
        reset(); files[name] = "head,models/shader\n";
        const auto handle = RE_RegisterSkin(name); CHECK(handle == 1);
        CHECK(tr.skins[handle]->numSurfaces == 1);
#ifdef JK2_MODE
        CHECK(!std::strcmp(tr.skins[handle]->surfaces[0]->shader->name,
            std::strlen(name) < 5 ? name : "models/shader"));
#else
        CHECK(!std::strcmp(tr.skins[handle]->surfaces[0]->name, "head"));
        CHECK(!std::strcmp(tr.skins[handle]->surfaces[0]->shader->name, "models/shader"));
#endif
        const int reads = fileReads;
        CHECK(RE_RegisterSkin(lower(name).c_str()) == handle && fileReads == reads);
    }
    reset(); files["mixed.skin"] = "head,models/shader\n";
    CHECK(RE_RegisterSkin("Mixed.SKIN") == 1);
#ifdef JK2_MODE
    // The legacy JK2 extension check is intentionally case-sensitive.
    CHECK(!std::strcmp(tr.skins[1]->surfaces[0]->shader->name, "Mixed.SKIN") && fileReads == 0);
#else
    CHECK(!std::strcmp(tr.skins[1]->surfaces[0]->shader->name, "models/shader"));
#endif
}
static void surfaces() {
    for (const char *surface : {"a", "ab", "abc", "head", "HEAD_OFF", "_off"}) {
        reset(); files["test.skin"] = std::string(surface) + ",models/shader\n";
        CHECK(RE_RegisterSkin("test.skin") == 1);
        std::string expected = lower(surface);
#ifndef JK2_MODE
        if (expected.size() >= 4 && expected.substr(expected.size() - 4) == "_off") expected.resize(expected.size() - 4);
#endif
        CHECK(tr.skins[1]->numSurfaces == 1);
        CHECK(tr.skins[1]->surfaces[0]->name == expected);
    }
    reset(); files["off.skin"] = "head_off,*off\nbody,models/shader\n";
    CHECK(RE_RegisterSkin("off.skin") == 1);
#ifdef JK2_MODE
    CHECK(tr.skins[1]->numSurfaces == 2);
#else
    CHECK(tr.skins[1]->numSurfaces == 1 && !std::strcmp(tr.skins[1]->surfaces[0]->name, "body"));
#endif
    for (const char *text : {"\"head", "head,\"models/shader", "/* comment */ \"HEAD\",\"models/shader\""}) {
        reset(); files["quoted.skin"] = text;
        CHECK(RE_RegisterSkin("quoted.skin") == 1);
        CHECK(tr.skins[1]->numSurfaces == 1);
    }
}
static bool split(const char *name, char *head, char *torso, char *lowerPart) {
#ifdef TEST_SPLIT_NO_SIZE
    return RE_SplitSkins(name, head, torso, lowerPart);
#else
    return RE_SplitSkins(name, head, torso, lowerPart, MAX_QPATH);
#endif
}
static void macroPaths() {
    char head[MAX_QPATH] = {}, torso[MAX_QPATH] = {}, lowerPart[MAX_QPATH] = {};
    CHECK(split("models/|head|torso|lower", head, torso, lowerPart));
    CHECK(!std::strcmp(head, "models/head.skin") && !std::strcmp(torso, "models/torso.skin") && !std::strcmp(lowerPart, "models/lower.skin"));
    CHECK(!split("normal.skin", head, torso, lowerPart));
    CHECK(!split("models/|head|torso", head, torso, lowerPart));
    const std::string longMacro = std::string(59, 'b') + "|a||";
    CHECK(longMacro.size() == MAX_QPATH - 1);
    CHECK(!split(longMacro.c_str(), head, torso, lowerPart));
    const std::string fits = std::string(57, 'b') + "|a||";
    CHECK(split(fits.c_str(), head, torso, lowerPart)); CHECK(std::strlen(head) == MAX_QPATH - 1);
}
int main() {
    registration(); surfaces(); macroPaths(); reset();
    std::puts("Production skin parser regressions passed.");
}
