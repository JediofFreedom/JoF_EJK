#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
typedef bool qboolean;
const bool qtrue = true, qfalse = false;
struct Cvar { int integer; float value; } toggle = {1, 1}, limit = {2, 2};
Cvar *s_soundAntiSpam = &toggle, *s_maxSounds = &limit;
struct { int realtime; } cls;
struct { int lastReset, maxSoundsPerSec; } sb;
static std::map<std::string, int> counts;
static int SFX_GetCount(const char *name) { return counts[name]; }
static void SFX_IncrementPlayCount(const char *name) { ++counts[name]; }
static void SFX_ResetAllCounts() { counts.clear(); }
#include "limiter.h"
int main() {
    const char *on = "sound/weapons/saber/saberon.wav";
    const char *off = "sound/weapons/saber/saberoff.wav";
    cls.realtime = 100;
    for (int i = 0; i < 3; ++i) CHECK(S_CanPlaySound(on));
    CHECK(!S_CanPlaySound(on));
    CHECK(S_CanPlaySound(off));
    toggle.integer = 0;
    for (int i = 0; i < 1000; ++i) {
        CHECK(S_CanPlaySound(on));
        CHECK(S_CanPlaySound(off));
    }
    toggle.integer = 1;
    for (int i = 0; i < 3; ++i) CHECK(S_CanPlaySound(on));
    CHECK(!S_CanPlaySound(on));
    cls.realtime += 1000;
    CHECK(S_CanPlaySound(on));
    counts[on] = 100;
    cls.realtime = 1;
    CHECK(S_CanPlaySound(on));
    std::puts("Sound anti-spam checks passed");
}
