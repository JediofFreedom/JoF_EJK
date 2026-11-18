#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

typedef int qboolean;
enum { qfalse, qtrue };
struct cvar_t { int integer; };
static cvar_t *cl_discordRichPresence;
static struct { qboolean discordInitialized; int sentinel; } cls;
static int disconnects, shutdowns, sounds, discordShutdowns, updates, chat;
static int input, console, runningResets, catchers, warnings, deletions;
static bool recurse;

static void Require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
struct VertexSpace { ~VertexSpace() { ++deletions; } };
static VertexSpace *G2VertSpaceClient;
void CL_Shutdown(void);
static void Com_Printf(const char *message) {
    if (std::strstr(message, "Recursive")) ++warnings;
}
static void CL_Disconnect(qboolean disconnect) {
    Require(disconnect == qtrue, "Disconnect argument changed");
    ++disconnects;
    if (recurse) { recurse = false; CL_Shutdown(); }
}
static void CL_ShutdownAll(qboolean shutdownRef) {
    Require(shutdownRef == qtrue, "Renderer shutdown argument changed"); ++shutdowns;
}
static void S_Shutdown() { ++sounds; }
static void Cmd_RemoveCommand(const char *) {}
static void CL_DiscordShutdown() { ++discordShutdowns; }
static void CL_Update_Shutdown() { ++updates; }
static void CL_SJK_Shutdown() { ++chat; }
static void CL_ShutdownInput() { ++input; }
static void Con_Shutdown() { ++console; }
static void Cvar_Set(const char *name, const char *value) {
    Require(!std::strcmp(name, "cl_running") && !std::strcmp(value, "0"),
            "Shutdown did not clear cl_running");
    ++runningResets;
}
static void Com_Memset(void *destination, int value, size_t size) {
    std::memset(destination, value, size);
}
static void Key_SetCatcher(int catcher) {
    Require(catcher == 0, "Shutdown did not release key catcher"); ++catchers;
}

#include "actual_shutdown.h"

int main(int argc, char **argv) {
#ifdef _WIN32
    // A pre-fix null dereference should fail CTest without a Windows error dialog.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    try {
        Require(argc == 2, "Expected one test case");
        const std::string part = argv[1];
        cvar_t setting = { 0 };
        int expectedDiscord = 0, passes = 1;
        if (part == "early") {
            // Filesystem failure before CL_Init: cvar and Discord state are zero.
        } else if (part == "disabled") {
            cl_discordRichPresence = &setting;
        } else if (part == "enabled") {
            setting.integer = 1; cl_discordRichPresence = &setting;
            cls.discordInitialized = qtrue; expectedDiscord = 1;
        } else if (part == "disabled_initialized") {
            cl_discordRichPresence = &setting;
            cls.discordInitialized = qtrue; expectedDiscord = 1;
        } else if (part == "null_initialized") {
            cls.discordInitialized = qtrue; expectedDiscord = 1;
        } else if (part == "repeated") {
            passes = 2;
        } else if (part == "recursive") {
            recurse = true;
        } else throw std::runtime_error("Unknown test case");

        cls.sentinel = 123;
        G2VertSpaceClient = new VertexSpace;
        for (int i = 0; i < passes; ++i) CL_Shutdown();
        Require(discordShutdowns == expectedDiscord, "Wrong Discord shutdown count");
        Require(disconnects == passes && shutdowns == passes && sounds == passes &&
                updates == passes && chat == passes && input == passes &&
                console == passes && runningResets == passes && catchers == passes,
                "Client cleanup did not finish exactly once per shutdown");
        Require(!G2VertSpaceClient && deletions == 1, "Vertex space was not freed exactly once");
        Require(!cls.discordInitialized && !cls.sentinel, "Client state was not cleared");
        Require(warnings == (part == "recursive" ? 1 : 0), "Recursive guard changed");
        std::printf("PASS %s (production CL_Shutdown, subsystem stubs)\n", argv[1]);
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1;
    }
}
