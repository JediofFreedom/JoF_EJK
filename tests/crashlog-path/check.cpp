#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

enum { MAX_OSPATH = 1024, SDL_MESSAGEBOX_ERROR = 1 };
static const char PATH_SEP = '/';
static const char *effectiveHome, *defaultHome, *installHome, *workingDirectory;
static int defaultCalls, installCalls, cwdCalls, writes, dialogs, mkdirCalls;
static std::string selectedDirectory, writtenPath, dialogMessage;
static void Require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
static const char *Cvar_VariableString(const char *name) {
    Require(!std::strcmp(name, "fs_homepath"), "Unexpected cvar lookup");
    return effectiveHome;
}
static const char *Sys_DefaultHomePath() { ++defaultCalls; return defaultHome; }
static const char *Sys_DefaultInstallPath() { ++installCalls; return installHome; }
static const char *Sys_Cwd() { ++cwdCalls; return workingDirectory; }
static void Sys_Mkdir(const char *path) {
    Require(path && *path, "Crash logging passed NULL/empty directory to mkdir");
    ++mkdirCalls; selectedDirectory = path;
#ifdef _WIN32
    const int result = _mkdir(path);
#else
    const int result = mkdir(path, 0700);
#endif
    Require(result == 0 || errno == EEXIST, "Could not create private fixture directory");
}
static void Com_sprintf(char *out, size_t size, const char *format, ...) {
    // Preserve the real formatting operation, but diagnose the old NULL input
    // deterministically rather than depend on a CRT's undefined %s behavior.
    va_list args, probe; va_start(args, format); va_copy(probe, args);
    const char *directory = va_arg(probe, const char *); va_end(probe);
    Require(directory && *directory, "Crash logging formatted a NULL/empty directory");
    const int length = std::vsnprintf(out, size, format, args); va_end(args);
    Require(length >= 0 && static_cast<size_t>(length) < size, "Crashlog path truncated");
    writtenPath = out;
}
static const char *va(const char *format, ...) {
    static char text[4096]; va_list args; va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args); va_end(args); return text;
}
static void ConsoleLogWriteOut(FILE *file) {
    // The engine filesystem is intentionally absent: the extracted production
    // dialog must still write using the native FILE after filesystem shutdown.
    Require(file != stderr, "Native crashlog unexpectedly fell back to stderr");
    ++writes;
    Require(std::fputs("fixture console after filesystem shutdown\n", file) >= 0,
            "Native FILE write failed");
}
static int SDL_ShowSimpleMessageBox(unsigned int flags, const char *title,
                                    const char *message, void *window) {
    Require(flags == SDL_MESSAGEBOX_ERROR && !std::strcmp(title, "Error") && !window,
            "Crash dialog arguments changed");
    ++dialogs; dialogMessage = message; return 0;
}

#include "actual_crashlog.h"

int main(int argc, char **argv) {
    try {
        Require(argc == 2, "Expected one test case"); const std::string part = argv[1];
        std::string expected; int expectedDefault = 0, expectedInstall = 0, expectedCwd = 0;
        effectiveHome = ""; defaultHome = "unused-default";
        installHome = "unused-install"; workingDirectory = "unused-working";
        if (part == "portable") {
            effectiveHome = "fixture-portable"; defaultHome = nullptr; expected = effectiveHome;
        } else if (part == "explicit") {
            effectiveHome = "fixture-explicit"; expected = effectiveHome;
        } else if (part == "normal_default") {
            defaultHome = "fixture-default"; expected = defaultHome; expectedDefault = 1;
        } else if (part == "default_null") {
            defaultHome = nullptr; installHome = "fixture-install"; expected = installHome;
            expectedDefault = expectedInstall = 1;
        } else if (part == "empty") {
            defaultHome = installHome = ""; workingDirectory = "fixture-working";
            expected = workingDirectory; expectedDefault = expectedInstall = expectedCwd = 1;
        } else if (part == "all_null") {
            effectiveHome = defaultHome = installHome = workingDirectory = nullptr;
            expected = "."; expectedDefault = expectedInstall = expectedCwd = 1;
        } else throw std::runtime_error("Unknown case");

        Sys_ErrorDialog("original fatal error");
        Require((defaultCalls != 0) == (expectedDefault != 0) &&
                (installCalls != 0) == (expectedInstall != 0) &&
                (cwdCalls != 0) == (expectedCwd != 0), "Crashlog path precedence changed");
        Require(mkdirCalls == 1 && selectedDirectory == expected, "Wrong crashlog directory");
        Require(writtenPath.find(expected + "/crashlog-") == 0, "Wrong native crashlog path");
        Require(writes == 1 && dialogs == 1, "Crashlog write/dialog count changed");
        Require(dialogMessage.find("original fatal error\n\nThe crash log was written to " +
                                   writtenPath) == 0, "Crashlog success message changed");
        FILE *file = std::fopen(writtenPath.c_str(), "r");
        Require(file != nullptr, "Native crashlog missing");
        char content[128] = {}; const size_t count = std::fread(content, 1, sizeof(content)-1, file);
        std::fclose(file);
        Require(count && std::string(content) == "fixture console after filesystem shutdown\n",
                "Native crashlog content differs");
        std::printf("PASS %s (actual Sys_ErrorDialog, native FILE write)\n", argv[1]); return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1;
    }
}
