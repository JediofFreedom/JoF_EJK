#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#define CHECK(test) do { if (!(test)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); std::exit(1); } } while (0)
enum { qfalse, qtrue, MAX_TOKEN_CHARS = 1024, SCREEN_WIDTH = 640 };
typedef int qboolean;

#include "filters.h"
static bool matches(const std::string &pattern, const std::string &name, bool caseSensitive = false) {
    // Exact-size buffers expose reads after the terminating NUL under ASan.
    std::unique_ptr<char[]> p(new char[pattern.size() + 1]), n(new char[name.size() + 1]);
    std::memcpy(p.get(), pattern.c_str(), pattern.size() + 1);
    std::memcpy(n.get(), name.c_str(), name.size() + 1);
    return Com_Filter(p.get(), n.get(), caseSensitive) != 0;
}
static void filters() {
    CHECK(!matches("?", ""));
    CHECK(!matches("??", "x"));
    CHECK(!matches("[abc", "a"));
    CHECK(!matches("[[", "["));
    CHECK(!matches("[a-z]", ""));
    CHECK(!matches("?x", ""));
    CHECK(matches("", "anything"));
    CHECK(matches("*", ""));
    CHECK(matches("a", "abc")); // Preserve the existing prefix-match contract.
    CHECK(matches("a?c", "abc"));
    CHECK(matches("[a-z]fg", "EFG"));
    CHECK(!matches("[a-z]fg", "EFG", true));
    CHECK(matches("*shader*", "textures/shader_abc"));
    CHECK(matches("*Long?Name", "prefix_longXname_suffix"));
    CHECK(matches("[]]]", "]"));
    CHECK(matches("*" + std::string(32768, 'a'), std::string(32768, 'a')));
    CHECK(!matches("*" + std::string(32768, 'a'), "short"));
    CHECK(matches(std::string(1, char(0xff)), std::string(1, char(0xff))));
    for (const char *pattern : {"[", "[a", "[a-", "[a-z", "[]", "[]]", "[[", "[[[", "?[", "*[?"})
        for (const char *name : {"", "a", "]", "[", "x"}) (void)matches(pattern, name);
}

struct Cvar { int integer; } fps, recordingMode;
static Cvar *com_renderfps = &fps, *cl_drawRecording = &recordingMode;
static struct { int frametime; float widthRatioCoef; } cls = {16, 1};
static struct { bool demorecording, spDemoRecording; int demofile; const char *demoName; } clc = {true, false, 1, "test"};
static float g_color_table[8][4];
static int tellCalls;
struct Draw { std::string text; float x, y; };
static std::vector<Draw> draws;
static int FS_FTell(int) { ++tellCalls; return 2048; }
static void Com_sprintf(char *out, size_t size, const char *format, ...) {
    va_list args; va_start(args, format); std::vsnprintf(out, size, format, args); va_end(args);
}
static void SCR_DrawStringExt2(float x, float y, float, float, const char *text, float *, qboolean, qboolean) {
    draws.push_back({text, x, y});
}
#include "recording.h"
static void recording() {
    for (int mode : {-100, -1, 0}) {
        recordingMode.integer = mode; tellCalls = 0; draws.clear();
        SCR_DrawDemoRecording(); CHECK(draws.empty() && !tellCalls);
    }
    const char *expected[] = {"RECORDING test: 2k", "test: 2k", "test", "REC", "REC"};
    for (int mode = 1; mode <= 5; ++mode) {
        recordingMode.integer = mode; draws.clear(); SCR_DrawDemoRecording();
        CHECK(draws.size() == 1 && draws[0].text == expected[mode - 1]);
        CHECK(draws[0].y == (mode == 5 ? 36 : 20));
    }
    fps.integer = 1; recordingMode.integer = 0; cls.frametime = 0;
    draws.clear(); SCR_DrawDemoRecording(); CHECK(draws.empty());
    cls.frametime = 10; SCR_DrawDemoRecording(); CHECK(draws.size() == 1 && draws[0].text == "100");
}

namespace serverTest {
typedef unsigned char byte;
enum { MAX_GENTITIES = 1024, SVFIXES_ALLOW_GHOSTED_PLAYERS = 2, ERR_DROP = 1 };
struct sharedEntity_t { struct { int number; } s; byte gamePrivate[28]; };
struct svEntity_t { int unused; };
static struct { sharedEntity_t *gentities; int gentitySize; svEntity_t svEntities[MAX_GENTITIES]; } sv;
static Cvar legacy;
static Cvar *sv_legacyFixes = &legacy;
#define S_COLOR_RED ""
static void Com_Printf(const char *, ...) {}
struct BadEntity {};
static void Com_Error(int code, const char *) { CHECK(code == ERR_DROP); throw BadEntity(); }
#include "server_entity.h"
static void check() {
    sharedEntity_t entities[16] = {};
    sv.gentities = entities; sv.gentitySize = sizeof(entities[0]);
    entities[3].s.number = 11;
    for (int mode : {0, 1, 2}) {
        legacy.integer = mode;
        CHECK(SV_SvEntityForGentity(&entities[3]) == &sv.svEntities[mode == 1 ? 3 : 11]);
        try { SV_SvEntityForGentity(nullptr); CHECK(false); } catch (const BadEntity &) {}
        for (int invalid : {-1, int(MAX_GENTITIES), MAX_GENTITIES + 100}) {
            entities[3].s.number = invalid;
            try { SV_SvEntityForGentity(&entities[3]); CHECK(false); } catch (const BadEntity &) {}
        }
        entities[3].s.number = 11;
    }
}
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!std::strcmp(argv[1], "filters")) filters();
    else if (!std::strcmp(argv[1], "recording")) recording();
    else if (!std::strcmp(argv[1], "server")) serverTest::check();
    else return 1;
    return 0;
}
