#include "qcommon/q_shared.h"
#include "mp3code/mp3struct.h"
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define CHECK(v) do { if (!(v)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#v); std::exit(1); } } while (0)
#include "types.inc"
static std::vector<std::string> printed;
void QDECL Com_Printf(const char *format,...) {
    char text[1024]; va_list args; va_start(args,format); std::vsnprintf(text,sizeof(text),format,args); va_end(args);
    printed.emplace_back(text);
}
void NORETURN QDECL Com_Error(int code,const char *,...) { throw code; }
#include "profile.inc"
static int soundStarted = 1,s_numSfx = 2,listener_number = 3,s_paintedtime;
static qboolean s_soundMuted;
static int &s_soundStarted = soundStarted;
static sfx_t s_knownSfx[2];
static channel_t channel;
static int s_entityWavVol[MAX_GENTITIES];
static cvar_t show;
static cvar_t *s_show = &show;
enum { SOUND_MAXVOL = 255 };
static const float SOUND_FMAXVOL = .5f;
static std::string calls;
static bool allowed = true;
static int channelMode;
static void S_memoryLoad(sfx_t *sound) { calls += 'L'; sound->bInMemory = qtrue; }
static void SND_TouchSFX(sfx_t *sound) { calls += 'T'; sound->iLastTimeUsed = 1001; sound->iLastLevelUsedOn = 7; }
static qboolean S_CanPlaySound(const char *) { calls += 'A'; return allowed ? qtrue : qfalse; }
static channel_t *S_PickChannel(int,int) {
    calls += 'P';
    if (channelMode == 1) return nullptr;
    if (channelMode == 2) throw int(ERR_DROP);
    std::memset(&channel,0,sizeof(channel)); return &channel;
}
void VectorCopy(const vec3_t from,vec3_t to) { std::memcpy(to,from,sizeof(vec3_t)); }
#include "start.inc"
static uint64_t ticks,clockCalls;
static uint64_t Clock() { ++clockCalls; return ticks += 100; }
static cvar_t enabled;
static void checkCount(SoundStartProfile::Stage stage,uint64_t calls) {
    CHECK(SoundStartProfile::counters[stage].calls == calls);
    CHECK(SoundStartProfile::counters[stage].nanoseconds == calls*100);
}
static void exercise() {
    using namespace SoundStartProfile;
    std::memset(s_knownSfx,0,sizeof(s_knownSfx));
    std::memset(s_entityWavVol,0,sizeof(s_entityWavVol));
    const vec3_t origin = {1,2,3};
    calls.clear(); S_StartSound(origin,1,CHAN_BODY,0);
    CHECK(calls == "LTAP");
    CHECK(channel.fixed_origin && channel.origin[0] == 1 && channel.origin[1] == 2 && channel.origin[2] == 3);
    CHECK(channel.thesfx == &s_knownSfx[0] && channel.entnum == 1 && channel.entchannel == CHAN_BODY);
    CHECK(channel.startSample == START_SAMPLE_IMMEDIATE && channel.master_vol == SOUND_MAXVOL);
    CHECK(s_knownSfx[0].iLastTimeUsed == 1001 && s_knownSfx[0].iLastLevelUsedOn == 7);
    MP3STREAM empty{}; CHECK(!std::memcmp(&channel.MP3StreamHeader,&empty,sizeof(empty)));
    allowed = false; calls.clear(); S_StartSound(nullptr,2,CHAN_BODY,0); CHECK(calls == "TA");
    allowed = true; channelMode = 1; calls.clear(); S_StartSound(nullptr,2,CHAN_BODY,0); CHECK(calls == "TAP");
    channelMode = 2; calls.clear();
    try { S_StartSound(nullptr,2,CHAN_BODY,0); CHECK(false); } catch (int code) { CHECK(code == ERR_DROP && calls == "TAP"); }
    channelMode = 0;
    static MP3STREAM mp3;
    std::memset(&mp3,0xa5,sizeof(mp3)); s_knownSfx[0].pMP3StreamHeader = &mp3;
    calls.clear(); S_StartSound(nullptr,listener_number,CHAN_VOICE,0); CHECK(calls == "TAP");
    CHECK(!channel.fixed_origin && channel.master_vol == int(SOUND_MAXVOL*SOUND_FMAXVOL));
    CHECK(s_entityWavVol[listener_number] == -1 && !std::memcmp(&channel.MP3StreamHeader,&mp3,sizeof(mp3)));
    const auto beforeClock = clockCalls;
    try { S_StartSound(nullptr,-1,CHAN_BODY,0); CHECK(false); } catch (int code) { CHECK(code == ERR_DROP); }
    try { S_StartSound(nullptr,1,CHAN_BODY,-1); CHECK(false); } catch (int code) { CHECK(code == ERR_DROP); }
    s_soundMuted = qtrue; S_StartSound(nullptr,1,CHAN_BODY,0); s_soundMuted = qfalse;
    CHECK(clockCalls == beforeClock);
}
int main() {
    using namespace SoundStartProfile;
    SoundStartProfile::clock = Clock; SoundStartProfile::enabled = &::enabled;
    Reset(); exercise(); CHECK(clockCalls == 0);
    Report(); CHECK(printed.empty());
    ::enabled.integer = 1;
    Reset(); exercise();
    checkCount(Load,1); checkCount(Touch,5); checkCount(Limiter,5); checkCount(Channel,4); checkCount(Setup,2);
    Report(); CHECK(printed.size() == Count);
    const auto stale = Begin(); Reset(); const auto beforeClock = clockCalls;
    End(Touch,stale); CHECK(clockCalls == beforeClock && !counters[Touch].calls);
    std::puts("Actual software sound submission: default-off clocks, five stages, early returns/errors, PCM/MP3 channel state and profiler reset checks passed.");
}
