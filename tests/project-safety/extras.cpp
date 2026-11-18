#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <rtcapi.h>
#endif

#define CHECK(test) do { if (!(test)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); std::exit(1); } } while (0)
#define VectorCopy(from, to) std::memcpy((to), (from), sizeof(vec3_t))
#define S_COLOR_YELLOW ""
typedef int qboolean;
typedef unsigned char byte;
typedef float vec3_t[3];
enum { qfalse, qtrue, MAX_QPATH = 64 };
static void Com_Printf(const char *, ...) {}
static void Com_sprintf(char *out, size_t size, const char *format, ...) {
    va_list args; va_start(args, format); std::vsnprintf(out, size, format, args); va_end(args);
}

namespace consoleTest {
struct Cvar { float value; };
static Cvar *con_scale;
static int padding() {
#include "console_padding.h"
    return padding;
}
static void check() {
    con_scale = nullptr; CHECK(padding() == 2);
    Cvar scale; con_scale = &scale;
    const float values[] = {-1, 0, 0.1f, 0.25f, 0.5f, 1, 1.25f, 1.5f, 2};
    const int expected[] = {2, 2, 0, 1, 1, 2, 3, 3, 4};
    for (int i = 0; i < 9; ++i) { scale.value = values[i]; CHECK(padding() == expected[i]); }
}
}

namespace pauseTest {
enum { MAX_CLIENTS = 8, TEAM_SPECTATOR = 3, PAUSE_NONE = 0 };
static struct Entity { bool inuse; } g_entities[MAX_CLIENTS];
static struct {
    struct { int state; } pause;
    struct { struct { bool raceMode; int sessionTeam; } sess; } clients[MAX_CLIENTS];
} level;
static std::vector<int> recipients;
static struct Services {
    static void SendServerCommand(int client, const char *message) {
        CHECK(!std::strcmp(message, "cp \"Fight!\n\"")); recipients.push_back(client);
    }
} services, *trap = &services;
static void expire() {
    int i = -999, j; // Sentinel makes use of the wrong loop variable observable.
    (void)i;
#include "pause_expiry.h"
}
static void check() {
    g_entities[1].inuse = g_entities[2].inuse = g_entities[3].inuse = true;
    level.clients[2].sess.raceMode = level.clients[3].sess.raceMode = true;
    level.clients[3].sess.sessionTeam = TEAM_SPECTATOR;
    level.pause.state = 1;
    expire(); CHECK(level.pause.state == PAUSE_NONE);
    CHECK(recipients == std::vector<int>({1, 3}));
}
}

namespace bspTest {
enum { EF_PERMANENT = 1, ET_MOVER = 4 };
struct Entity {
    const char *classname;
    struct {
        vec3_t origin, angles;
        struct { vec3_t trBase; } pos, apos;
        int eFlags, time2, time, eType, modelindex;
    } s;
    struct { vec3_t currentOrigin, currentAngles; } r;
};
typedef Entity gentity_t;
typedef Entity sharedEntity_t;
static struct { int integer; } sv_cheats;
static struct {
    int mNumBSPInstances;
    vec3_t mOriginAdjust;
    float mRotationAdjust;
    const char *mTargetAdjust;
    int mBSPInstanceDepth;
    char mTeamFilter[MAX_QPATH];
} level;
static Entity spawned;
static int spawns, links;
static Entity *G_Spawn(qboolean) { ++spawns; std::memset(&spawned, 0, sizeof(spawned)); return &spawned; }
static void G_BSPIndex(const char *) {}
static void G_SpawnEntitiesFromString(qboolean) {}
static struct Services {
    static int Argc() { return 2; }
    static void Argv(int, char *out, size_t size) { std::snprintf(out, size, "1"); }
    static void SetBrushModel(Entity *, const char *name) { CHECK(!std::strcmp(name, "#mp/duel1")); }
    static void LinkEntity(Entity *) { ++links; }
    static void SetActiveSubBSP(int) {}
} services, *trap = &services;
#include "bsp_command.h"
static void check() {
    sv_cheats.integer = 0; Svcmd_MiscBsp_f(); CHECK(!spawns);
    sv_cheats.integer = 1; Svcmd_MiscBsp_f(); CHECK(spawns == 1 && links == 1);
    for (int axis = 0; axis < 3; ++axis) {
        CHECK(spawned.s.origin[axis] == 0 && spawned.s.pos.trBase[axis] == 0);
        CHECK(spawned.r.currentOrigin[axis] == 0 && level.mOriginAdjust[axis] == 0);
    }
    CHECK(level.mBSPInstanceDepth == 0);
}
}

namespace shaderTest {
static struct { const char *name; } shader = {"fixture"};
static std::vector<std::string> tokens;
static size_t next;
static char *COM_ParseExt(const char **, qboolean) {
    static char empty[] = "";
    return next < tokens.size() ? &tokens[next++][0] : empty;
}
#include "shader_vector.h"
enum { CGEN_CONST = 1 };
struct Stage { byte constantColor[4]; int rgbGen; };
static qboolean parseConst(const char **text, Stage *stage) {
#include "shader_const.h"
    return qtrue;
}
static bool parse(const char *source, Stage &stage) {
    tokens.clear(); next = 0;
    std::istringstream stream(source); std::string token;
    while (stream >> token) tokens.push_back(token);
    return parseConst(&source, &stage) != 0;
}
static void check() {
    Stage stage = {{7, 8, 9, 255}, -1};
    CHECK(parse("( 0.25 0.5 1 )", stage));
    CHECK(stage.constantColor[0] == 63 && stage.constantColor[1] == 127 && stage.constantColor[2] == 255);
    CHECK(stage.rgbGen == CGEN_CONST);
    for (const char *bad : {"", "1 2 3", "( 1", "( 1 2", "( 1 2 3", "( 1 2 3 ]"}) {
        Stage rejected = {{7, 8, 9, 255}, -1};
        CHECK(!parse(bad, rejected));
        CHECK(rejected.constantColor[0] == 7 && rejected.constantColor[1] == 8 && rejected.constantColor[2] == 9);
    }
}
}

#if defined(_MSC_VER) && defined(_DEBUG)
static int __cdecl runtimeCheck(int, const char *, int, const char *, const char *, ...) {
    std::fputs("MSVC runtime check detected undefined fixture behavior.\n", stderr); std::exit(2);
}
#endif
int main(int argc, char **argv) {
#if defined(_MSC_VER) && defined(_DEBUG)
    _RTC_SetErrorFunc(runtimeCheck);
#endif
    CHECK(argc == 2);
    if (!std::strcmp(argv[1], "padding")) consoleTest::check();
    else if (!std::strcmp(argv[1], "pause")) pauseTest::check();
    else if (!std::strcmp(argv[1], "bsp")) bspTest::check();
    else if (!std::strcmp(argv[1], "shader")) shaderTest::check();
    else return 1;
    return 0;
}
