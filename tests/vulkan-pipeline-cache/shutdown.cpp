#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cctype>
#define CHECK(v) do { if (!(v)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #v); std::exit(1); } } while(0)
typedef int qboolean;
const int qtrue = 1, qfalse = 0;
static char activeRendererName[64];
static void *rendererLib;
static std::string calls;
static qboolean destroyArgument, restartArgument;
struct refexport_t { void (*Shutdown)(qboolean, qboolean); } api, *re;
static int Q_stricmp(const char *a, const char *b) {
    while (*a && std::tolower(static_cast<unsigned char>(*a)) == std::tolower(static_cast<unsigned char>(*b))) { ++a; ++b; }
    return std::tolower(static_cast<unsigned char>(*a)) - std::tolower(static_cast<unsigned char>(*b));
}
static void shutdown(qboolean destroy, qboolean restart) {
    CHECK(calls.empty());
    calls += "shutdown "; destroyArgument = destroy; restartArgument = restart;
}
static void Sys_UnloadDll(void *module) {
    CHECK(module == rendererLib && re == nullptr);
    calls += "unload ";
}
static void Cvar_DeactivateRendererCvars() { calls += "cvars"; }
#include "shutdown.h"

static void check(const char *name, qboolean restart, qboolean destroy) {
    std::strcpy(activeRendererName, name);
    rendererLib = &api;
    api.Shutdown = shutdown;
    re = &api;
    calls.clear();
    CL_ShutdownRef(restart);
    CHECK(calls == "shutdown unload cvars");
    CHECK(destroyArgument == destroy && restartArgument == restart);
    CHECK(!re && !rendererLib && !activeRendererName[0]);
}
int main() {
    check("rd-vulkan", qtrue, qtrue);
    check("RD-VULKAN", qtrue, qtrue);
    check("rd-vulkan", qfalse, qtrue);
    check("rd-eternaljk", qtrue, qfalse);
    check("rd-rend2-etjk", qtrue, qfalse);
    check("rd-eternaljk", qfalse, qtrue);
    calls.clear();
    CL_ShutdownRef(qfalse); // Repeated/partial cleanup is safe.
    CHECK(calls == "cvars");
    std::puts("Vulkan device teardown precedes DLL unload, including video restarts.");
}
