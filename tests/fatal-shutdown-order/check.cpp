#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#define QDECL
#ifdef _MSC_VER
#define NORETURN __declspec(noreturn)
#else
#define NORETURN __attribute__((noreturn))
#endif

enum { ERR_FATAL, ERR_DROP, ERR_SERVERDISCONNECT, ERR_DISCONNECT, ERR_NEED_CD };
enum { qfalse, qtrue, CVAR_ROM = 16 };
struct cvar_t { int integer; };
static cvar_t buildScript = {}, dedicated = {};
static cvar_t *com_buildScript = &buildScript, *com_dedicated = &dedicated;
static int com_errorEntered;
static char com_errorMessage[1024];
static int milliseconds = 1000, clockCalls, cvarGets, cvarSets;
static bool rendererExportsAlive = true, serverModelsAlive = true;
static std::vector<std::string> events;
static std::string serverMessage, storedMessage;

struct FatalExit { std::string message; };
struct QuitExit {};
static void Require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
static int Sys_Milliseconds() { ++clockCalls; return milliseconds; }
static int Q_vsnprintf(char *out, size_t size, const char *fmt, va_list args) {
    return std::vsnprintf(out, size, fmt, args);
}
static const char *va(const char *fmt, ...) {
    static char buffer[2048]; va_list args; va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args); va_end(args); return buffer;
}
static cvar_t *Cvar_Get(const char *name, const char *value, int flags) {
    Require(!std::strcmp(name, "com_errorMessage") && !*value && flags == CVAR_ROM,
            "Error cvar registration changed");
    ++cvarGets; return &buildScript;
}
static void Cvar_Set(const char *name, const char *value) {
    Require(!std::strcmp(name, "com_errorMessage"), "Wrong error cvar");
    ++cvarSets; storedMessage = value;
}
static void SV_Shutdown(const char *message) {
    events.push_back("server");
    // The supplied crash/PDB evidence resolves the server cleanup through a
    // renderer Ghoul2 export. Model that dependency without replacing Com_Error.
    Require(rendererExportsAlive, "Server cleanup called an invalidated renderer export");
    serverModelsAlive = false; serverMessage = message;
}
static void CL_Shutdown() {
    events.push_back("client"); rendererExportsAlive = false;
}
static void Com_Shutdown() { events.push_back("common"); }
static void FS_Shutdown(int closeMissing) {
    Require(closeMissing == qtrue, "Quit filesystem argument changed");
    events.push_back("files");
}
[[noreturn]] static void Sys_Error(const char *fmt, ...) {
    char message[2048]; va_list args; va_start(args, fmt);
    std::vsnprintf(message, sizeof(message), fmt, args); va_end(args);
    events.push_back("sys_error"); throw FatalExit{message};
}
[[noreturn]] static void Sys_Quit() { events.push_back("quit"); throw QuitExit{}; }

#include "actual_error.h"

static void Reset(int time = 1000) {
    buildScript.integer = dedicated.integer = 0; com_errorEntered = qfalse;
    com_errorMessage[0] = '\0'; milliseconds = time;
    clockCalls = cvarGets = cvarSets = 0;
    rendererExportsAlive = serverModelsAlive = true;
    events.clear(); serverMessage.clear(); storedMessage.clear();
}
static void Fatal(int code) {
    bool exited = false;
    try { Com_Error(code, "fatal %s %d", "fixture", 7); }
    catch (const FatalExit &exit) {
        exited = true; Require(exit.message == "fatal fixture 7", "Fatal message changed");
    }
    Require(exited, "Fatal path did not reach nonreturning Sys_Error");
    Require(events == std::vector<std::string>{"server", "client", "common", "sys_error"},
            "Fatal shutdown lifecycle order changed");
    Require(!rendererExportsAlive && !serverModelsAlive, "Fatal resources not released");
    Require(serverMessage == "Server fatal crashed: fatal fixture 7\n", "Server fatal message changed");
    Require(storedMessage == "fatal fixture 7" && cvarGets == 1 && cvarSets == 1,
            "Fatal cvar/message handling changed");
    Require(com_errorEntered == qtrue && clockCalls == 1, "Fatal guard/timing changed");
}
static void QuitControl() {
    Reset(2000); bool exited = false;
    try { Com_Quit_f(); } catch (const QuitExit &) { exited = true; }
    Require(exited && events == std::vector<std::string>{"server", "client", "common", "files", "quit"},
            "Normal quit lifecycle positive control failed");
    Require(serverMessage == "Server quit\n" && !serverModelsAlive && !rendererExportsAlive,
            "Quit resources/message changed");
}
static void EscalationControl() {
    Reset(3000);
    for (int i = 0; i != 5; ++i) {
        // The caller clears the recovery guard between independent frame errors.
        com_errorEntered = qfalse; milliseconds = 3000 + i * 10;
        bool recovered = false, fatal = false;
        try { Com_Error(ERR_DROP, "rapid %d", i); }
        catch (int code) { Require(code == ERR_DROP, "Wrong recoverable error"); recovered = true; }
        catch (const FatalExit &exit) { fatal = true; Require(exit.message == "rapid 4", "Escalation message changed"); }
        Require(i < 4 ? recovered && !fatal : fatal && !recovered, "Rapid error escalation threshold changed");
        if (i < 4) Require(events.empty() && rendererExportsAlive && serverModelsAlive,
                           "Recoverable drop shut down resources");
    }
    Require(events == std::vector<std::string>{"server", "client", "common", "sys_error"},
            "Escalated fatal shutdown lifecycle order changed");
}
static void RecoverableControl() {
    const int codes[] = {ERR_DISCONNECT, ERR_SERVERDISCONNECT, ERR_DROP, ERR_NEED_CD};
    int time = 10000;
    for (int expected : codes) {
        Reset(time); time += 1000; bool caught = false;
        try { Com_Error(expected, "recover %d", expected); }
        catch (int actual) { Require(actual == expected, "Recoverable error exception changed"); caught = true; }
        Require(caught && events.empty() && rendererExportsAlive && serverModelsAlive,
                "Recoverable error performed fatal shutdown");
        Require(com_errorEntered == qtrue && clockCalls == 1, "Recoverable entry guard changed");
        const bool stores = expected != ERR_DISCONNECT && expected != ERR_NEED_CD;
        Require(cvarGets == (stores ? 1 : 0) && cvarSets == (stores ? 1 : 0),
                "Recoverable message-cvar policy changed");
        if (stores) Require(storedMessage == com_errorMessage, "Recoverable stored message changed");
    }
}
static void RecursiveControl() {
    Reset(); com_errorEntered = qtrue; std::strcpy(com_errorMessage, "original failure");
    bool exited = false;
    try { Com_Error(ERR_FATAL, "must not replace original"); }
    catch (const FatalExit &exit) {
        exited = true; Require(exit.message == "recursive error after: original failure", "Recursive message changed");
    }
    Require(exited && events == std::vector<std::string>{"sys_error"}, "Recursive shutdown guard changed");
    Require(!clockCalls && !cvarGets && !cvarSets && rendererExportsAlive && serverModelsAlive,
            "Recursive error entered cleanup or modified state");
    Require(!std::strcmp(com_errorMessage, "original failure"), "Recursive error overwrote message");
    events.clear(); exited = false;
    try { Com_Quit_f(); } catch (const QuitExit &) { exited = true; }
    Require(exited && events == std::vector<std::string>{"quit"}, "Recursive quit positive control failed");
}
int main(int argc, char **argv) {
    try {
        Require(argc == 2, "Expected one test case"); Reset(); const std::string part = argv[1];
        if (part == "fatal") { Fatal(ERR_FATAL); QuitControl(); EscalationControl(); }
        else if (part == "buildscript") { buildScript.integer = 1; Fatal(ERR_DROP); }
        else if (part == "dedicated") { dedicated.integer = 1; Fatal(ERR_SERVERDISCONNECT); }
        else if (part == "recoverable") RecoverableControl();
        else if (part == "recursive") RecursiveControl();
        else throw std::runtime_error("Unknown test case");
        std::printf("PASS %s (actual production error/quit functions)\n", argv[1]); return 0;
    } catch (const std::exception &error) { std::fprintf(stderr, "FAIL: %s\n", error.what()); }
      catch (...) { std::fprintf(stderr, "FAIL: unexpected error/termination path\n"); }
    return 1;
}
