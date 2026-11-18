#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

static unsigned checks;
#define CHECK(c) do { ++checks; if (!(c)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)
typedef int qboolean;
enum { qfalse, qtrue, h_low, PRINT_ALL, ERR_FATAL, CVAR_ARCHIVE_ND };
const int MAX_QPATH = 64, MAX_TOKEN_CHARS = 1024, MAX_SHADERTEXT_HASH = 2048;
const char PATH_SEP = '/';
struct cvar_t { int integer; } setting = {1};
struct fsReadFileIdentity_t {
    uint64_t generation, container, position, size;
    uint32_t provider, reserved;
};
static int journal;
static size_t readCalls, identityCalls, parseCalls;
static std::set<std::string> referenced;
struct Input { std::string text; fsReadFileIdentity_t identity; bool readable; };
static std::map<std::string, Input> inputs;
static std::vector<std::string> order;
static std::vector<void *> hunks;
static char *s_shaderText;
static const char **shaderTextHashTable[MAX_SHADERTEXT_HASH];
static void Com_Error(int, const char *text) { throw std::runtime_error(text); }
static void Com_sprintf(char *out, size_t size, const char *format, ...) {
    va_list ap; va_start(ap, format); std::vsnprintf(out, size, format, ap); va_end(ap);
}
#include "engine_strings.h"
static char com_token[MAX_TOKEN_CHARS], com_parsename[MAX_TOKEN_CHARS];
static int com_lines, com_tokenline;
static char *RealCOM_ParseExt(const char **, qboolean);
static char *COM_ParseExt(const char **text, qboolean breaks) { ++parseCalls; return RealCOM_ParseExt(text, breaks); }
#include "parser.h"
static void vk_debug(const char *, ...) {}
static void Print(int, const char *, ...) {}
static cvar_t *GetSetting(const char *name, const char *, int, const char *) { CHECK(std::strcmp(name, "r_shaderTextCache") == 0); return &setting; }
static void CheckRange(cvar_t *, float, float, qboolean) {}
static int Integer(const char *name) { return std::strcmp(name, "journal") == 0 ? journal : 0; }
static char **List(const char *, const char *, int *count) {
    *count = int(order.size());
    char **names = static_cast<char **>(std::calloc(order.size() + 1, sizeof(char *)));
    for (size_t i = 0; i < order.size(); ++i) {
        names[i] = static_cast<char *>(std::malloc(order[i].size() + 1));
        std::strcpy(names[i], order[i].c_str());
    }
    return names;
}
static void FreeList(char **list) { for (char **p = list; *p; ++p) std::free(*p); std::free(list); }
static long Read(const char *path, void **out) {
    ++readCalls;
    auto found = inputs.find(path);
    if (found == inputs.end() || !found->second.readable) { *out = NULL; return -1; }
    referenced.insert(path);
    const std::string &text = found->second.text;
    *out = std::malloc(text.size() + 1);
    std::memcpy(*out, text.c_str(), text.size() + 1);
    return long(text.size());
}
static void FreeFile(void *memory) { std::free(memory); }
static qboolean Identity(const char *path, fsReadFileIdentity_t *out, qboolean mark) {
    ++identityCalls;
    auto found = inputs.find(path);
    if (found == inputs.end() || !found->second.readable || !found->second.identity.provider) { *out = {}; return qfalse; }
    *out = found->second.identity;
    if (mark) referenced.insert(path);
    return qtrue;
}
static void *Hunk(size_t bytes, int) {
    void *memory = std::calloc(1, bytes ? bytes : 1);
    if (!memory) throw std::bad_alloc();
    hunks.push_back(memory); return memory;
}
static void Error(int, const char *text) { throw std::runtime_error(text); }
static struct {
    cvar_t *(*Cvar_Get)(const char *, const char *, int, const char *);
    void (*Cvar_CheckRange)(cvar_t *, float, float, qboolean);
    int (*Cvar_VariableIntegerValue)(const char *);
    char **(*FS_ListFiles)(const char *, const char *, int *);
    void (*FS_FreeFileList)(char **);
    long (*FS_ReadFile)(const char *, void **);
    void (*FS_FreeFile)(void *);
    qboolean (*FS_GetReadFileIdentity)(const char *, fsReadFileIdentity_t *, qboolean);
    void *(*Hunk_Alloc)(size_t, int);
    void (*Printf)(int, const char *, ...);
    void (*Error)(int, const char *);
} ri = {GetSetting, CheckRange, Integer, List, FreeList, Read, FreeFile, Identity, Hunk, Print, Error};
#include "shader.h"

static void clearHunk() {
    for (void *memory : hunks) std::free(memory);
    hunks.clear(); s_shaderText = NULL;
    std::memset(shaderTextHashTable, 0, sizeof(shaderTextHashTable));
}
struct Output { std::string text; std::vector<std::vector<size_t>> buckets; std::set<std::string> refs; };
static Output scan() {
    clearHunk(); readCalls = identityCalls = parseCalls = 0; referenced.clear();
    ScanAndLoadShaderFiles();
    Output result; result.text = s_shaderText; result.refs = referenced;
    for (int bucket = 0; bucket < MAX_SHADERTEXT_HASH; ++bucket) {
        std::vector<size_t> offsets;
        CHECK(shaderTextHashTable[bucket] != NULL);
        for (const char **p = shaderTextHashTable[bucket]; *p; ++p) {
            CHECK(*p >= s_shaderText && *p < s_shaderText + result.text.size());
            offsets.push_back(size_t(*p - s_shaderText));
        }
        result.buckets.push_back(offsets);
    }
    return result;
}
static void same(const Output &a, const Output &b) {
    CHECK(a.text == b.text); CHECK(a.buckets == b.buckets); CHECK(a.refs == b.refs);
}
static void add(const std::string &name, const std::string &text, uint64_t id) {
    order.push_back(name);
    inputs["shaders/" + name] = {text, {id, id + 10, id + 20, text.size(), 1, 0}, true};
}
static void checkAgainstFresh() {
    Output candidate = scan();
    setting.integer = 0; Output control = scan(); same(candidate, control);
    setting.integer = 1; Output rebuilt = scan(); same(rebuilt, control);
    CHECK(readCalls == order.size());
    Output reused = scan(); same(reused, control);
    CHECK(readCalls == 0); CHECK(parseCalls == 0); CHECK(identityCalls == order.size());
}
int main() {
    add("a.shader", "// one\ntextures/shared { { map textures/a } }\ntextures/alpha { { map \"textures/a b\" } }", 1);
    add("b.shader", "# deprecated comment\ntextures/shared { { map textures/b } }\ntextures/bravo { /*comment*/ { map $lightmap } }", 2);
    // Different names deliberately share the extension-insensitive hash bucket.
    add("c.shader", "textures/name.one { { map textures/c } }\ntextures/name.two { { map textures/d } }", 3);
    add("empty.shader", "", 4);
    add("ignored.shader", "textures/unsupported missing-brace", 5);
    const char withNul[] = "textures/visible { { map textures/visible } }\0textures/hidden { { map textures/hidden } }";
    add("nul.shader", std::string(withNul, sizeof(withNul) - 1), 6);
    checkAgainstFresh();
    CHECK(FindShaderInShaderText("textures/visible") != NULL);
    CHECK(FindShaderInShaderText("textures/hidden") == NULL);
    CHECK(FindShaderInShaderText("textures/unsupported") == NULL);
    const char *shared = FindShaderInShaderText("textures/shared");
    CHECK(shared && std::strstr(shared, "textures/b"));
    for (int round = 0; round < 8; ++round) { Output reuse = scan(); CHECK(readCalls == 0 && parseCalls == 0); }

    std::reverse(order.begin(), order.end()); checkAgainstFresh();
    add("d.shader", "textures/delta { { map textures/new } }", 4); checkAgainstFresh();
    order.erase(order.begin()); checkAgainstFresh();
    Input &changed = inputs["shaders/a.shader"];
    changed.text = "textures/replaced { { map textures/new } }";
    ++changed.identity.generation; changed.identity.size = changed.text.size(); checkAgainstFresh();
    ++changed.identity.container; checkAgainstFresh();
    ++changed.identity.position; checkAgainstFresh();
    ++changed.identity.size; checkAgainstFresh();

    for (uint32_t provider : {uint32_t(2), uint32_t(0)}) {
        changed.identity.provider = provider;
        Output first = scan(); CHECK(readCalls == order.size()); CHECK(shaderCatalogueCache.text.empty());
        changed.text += "\ntextures/live { { map textures/live } }";
        Output edited = scan(); CHECK(readCalls == order.size()); CHECK(first.text != edited.text);
        setting.integer = 0; same(edited, scan()); setting.integer = 1;
    }
    changed.identity.provider = 1; changed.identity.size = changed.text.size(); checkAgainstFresh();
    changed.readable = false;
    Output denied = scan(); CHECK(readCalls == order.size()); CHECK(shaderCatalogueCache.text.empty());
    setting.integer = 0; same(denied, scan()); setting.integer = 1;
    changed.readable = true; checkAgainstFresh();

    for (int mode : {1, 2}) {
        journal = mode; Output recorded = scan(); CHECK(readCalls == order.size()); CHECK(identityCalls == 0);
        same(recorded, scan()); CHECK(readCalls == order.size()); CHECK(shaderCatalogueCache.text.empty());
    }
    journal = 0; checkAgainstFresh();
    auto identityService = ri.FS_GetReadFileIdentity; ri.FS_GetReadFileIdentity = NULL;
    Output withoutService = scan(); CHECK(readCalls == order.size()); CHECK(shaderCatalogueCache.text.empty());
    same(withoutService, scan()); CHECK(readCalls == order.size()); ri.FS_GetReadFileIdentity = identityService;
    checkAgainstFresh(); R_ClearShaderTextCache(); CHECK(shaderCatalogueCache.text.empty());
    scan(); CHECK(readCalls == order.size());

    // Retention is bounded independently of the existing loader's input size.
    clearHunk(); std::string oversized((sizeof(void *) == 8 ? 32 : 8) * size_t(1024 * 1024), ' ');
    s_shaderText = &oversized[0]; std::vector<ShaderCatalogueSource> sources;
    RetainShaderCatalogue(sources, 0); CHECK(shaderCatalogueCache.text.empty());
    s_shaderText = NULL;
    R_ClearShaderTextCache(); clearHunk();
    std::printf("shader catalogue: %u checks, exact text/buckets/references; unchanged loads avoid all asset reads and token parsing\n", checks);
}
