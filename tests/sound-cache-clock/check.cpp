#include "qcommon/qcommon.h"
#include "mp3code/mp3struct.h"
#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#define CHECK(v) do { if (!(v)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#v); std::exit(1); } } while (0)
#include "types.inc"

static cvar_t journalSetting;
cvar_t *com_journal = &journalSetting;
fileHandle_t com_journalFile = 1,com_journalDataFile = 2;
static int milliseconds,rawCalls,legacyCalls,consolePolls,fileReads,fileWrites;
int Sys_Milliseconds(bool baseTime) { ++rawCalls; return milliseconds; }
static bool consoleReady;
char *Sys_ConsoleInput(void) {
    ++consolePolls;
    static char command[] = "echo cache clock";
    if (!consoleReady) return nullptr;
    consoleReady = false; return command;
}
void QDECL Com_Printf(const char *,...) {}
void QDECL Com_DPrintf(const char *,...) {}
void NORETURN QDECL Com_Error(int code,const char *,...) { throw code; }
struct Allocation { int bytes; memtag_t tag; };
alignas(std::max_align_t) static byte arena[1024*1024];
static size_t arenaUsed;
static std::map<void *,Allocation> allocations;
void *Z_Malloc(int bytes,memtag_t tag,qboolean zero,int align) {
    CHECK(bytes >= 0 && align > 0);
    arenaUsed = (arenaUsed+size_t(align)-1)/size_t(align)*size_t(align);
    CHECK(arenaUsed+size_t(bytes) <= sizeof(arena));
    void *result = arena+arenaUsed; arenaUsed += size_t(bytes);
    CHECK(allocations.emplace(result,Allocation{bytes,tag}).second);
    if (zero) std::memset(result,0,size_t(bytes));
    return result;
}
void Z_Free(void *pointer) { if (pointer) CHECK(allocations.erase(pointer) == 1); }
int Z_Size(void *pointer) { CHECK(allocations.count(pointer)); return allocations.at(pointer).bytes; }
int Z_MemSize(memtag_t tag) {
    int total = 0; for (const auto &allocation : allocations) if (allocation.second.tag == tag) total += allocation.second.bytes;
    return total;
}
static std::vector<byte> journal;
static size_t journalPosition;
int FS_Write(const void *data,int length,fileHandle_t file) {
    CHECK(file == 1 && length >= 0); ++fileWrites;
    const auto *bytes = static_cast<const byte *>(data); journal.insert(journal.end(),bytes,bytes+length); return length;
}
int FS_Read(void *data,int length,fileHandle_t file) {
    CHECK(file == 1 && length >= 0 && journalPosition+size_t(length) <= journal.size()); ++fileReads;
    std::memcpy(data,journal.data()+journalPosition,size_t(length)); journalPosition += size_t(length); return length;
}
#define MAX_PUSHED_EVENTS 1024
static int com_pushedEventsHead,com_pushedEventsTail;
static sysEvent_t com_pushedEvents[MAX_PUSHED_EVENTS];
static std::mutex pushLock;
#define MAX_QUED_EVENTS 256
#define MASK_QUED_EVENTS (MAX_QUED_EVENTS-1)
static sysEvent_t eventQue[MAX_QUED_EVENTS];
static sysEvent_t *lastEvent;
static uint32_t eventHead,eventTail;
sysEvent_t Com_GetRealEvent(void);
void Com_PushEvent(sysEvent_t *);
sysEvent_t Com_GetEvent(void);
void Sys_QueEvent(int,sysEventType_t,int,int,int,void *);
#include "clocks.inc"

static int level = 7;
static int MediaLevel() { return level; }
static struct { int (*RegisterMedia_GetLevel)(); } renderer = {MediaLevel},*re = &renderer;
// The null-journal contract is tested as an uninitialized clock-service call;
// real Com_GetRealEvent itself requires initialized journal state. All initialized
// cases below forward through the actual production event/journal machinery.
static int TouchLegacyClock() {
    ++legacyCalls;
    return com_journal ? Com_Milliseconds() : Sys_Milliseconds();
}
#define Com_Milliseconds TouchLegacyClock
#define SND_TouchSFX ReferenceTouch
#include "reference.h"
#undef SND_TouchSFX
#include "touch.inc"
#undef Com_Milliseconds
static sfx_t s_knownSfx[8];
static int s_numSfx = 8;
static channel_t s_channels[MAX_CHANNELS];
static cvar_t poolSetting;
static cvar_t *s_soundpoolmegs = &poolSetting;
qboolean gbInsideLoadSound;
#include "cache.inc"

typedef void (*Touch)(sfx_t *);
struct Event {
    int time,type,value,value2;
    std::string payload;
    bool operator==(const Event &other) const {
        return time == other.time && type == other.type && value == other.value && value2 == other.value2 && payload == other.payload;
    }
};
struct Result {
    std::vector<int> timestamps,levels,evicted;
    std::vector<Event> delivered;
    std::vector<byte> recorded;
    int legacy = 0,polls = 0,reads = 0,writes = 0;
};
static void reset(int mode,const std::vector<byte> &recorded = {}) {
    CHECK(allocations.empty()); arenaUsed = 0;
    std::memset(eventQue,0,sizeof(eventQue)); eventHead = eventTail = 0; lastEvent = nullptr;
    Com_InitPushEvent();
    std::memset(s_knownSfx,0,sizeof(s_knownSfx)); std::memset(s_channels,0,sizeof(s_channels));
    journalSetting.integer = mode; com_journal = &journalSetting;
    milliseconds = 100; level = 7; consoleReady = false;
    rawCalls = legacyCalls = consolePolls = fileReads = fileWrites = 0;
    journal = recorded; journalPosition = 0; gbInsideLoadSound = qfalse; poolSetting.integer = 0;
}
static void queueInput() {
    Sys_QueEvent(12,SE_KEY,44,1,0,nullptr);
    Sys_QueEvent(13,SE_CHAR,'x',0,0,nullptr);
    Sys_QueEvent(14,SE_MOUSE,1,2,0,nullptr);
    Sys_QueEvent(15,SE_MOUSE,3,4,0,nullptr);
    char *command = static_cast<char *>(Z_Malloc(5,TAG_EVENT,qfalse));
    std::memcpy(command,"test",5);
    Sys_QueEvent(16,SE_CONSOLE,0,0,5,command);
}
static std::vector<Event> drainInput() {
    std::vector<Event> result;
    for (;;) {
        const auto event = Com_GetEvent();
        if (event.evType == SE_NONE) break;
        result.push_back(Event{event.evTime,event.evType,event.evValue,event.evValue2,
            event.evPtr ? std::string(static_cast<char *>(event.evPtr),size_t(event.evPtrLength)) : std::string()});
        if (event.evPtr) Z_Free(event.evPtr);
    }
    return result;
}
static void evict(Result &result,int protectedSound) {
    bool before[8]; for (int i = 0; i < 8; ++i) before[i] = s_knownSfx[i].bInMemory != 0;
    CHECK(SND_FreeOldestSound(&s_knownSfx[protectedSound]) == 64);
    for (int i = 0; i < 8; ++i) if (before[i] && !s_knownSfx[i].bInMemory) result.evicted.push_back(i);
}
static Result sequence(Touch touch,int mode,const std::vector<byte> &recorded = {}) {
    reset(mode,recorded);
    Result result;
    for (int i = 1; i < 8; ++i) {
        auto &sound = s_knownSfx[i]; sound.bInMemory = qtrue; sound.iLastTimeUsed = 10+i; sound.iLastLevelUsedOn = 7;
        sound.pSoundData = static_cast<short *>(Z_Malloc(64,TAG_SND_RAWDATA,qtrue));
    }
    s_knownSfx[3].pMP3StreamHeader = static_cast<MP3STREAM *>(Z_Malloc(sizeof(MP3STREAM),TAG_SND_MP3STREAMHDR,qtrue));
    s_knownSfx[6].bDefaultSound = qtrue;
    s_channels[0].thesfx = &s_knownSfx[2];
    if (mode != 2) queueInput();
    for (int i : {1,1,3}) {
        milliseconds += i;
        const int beforeLegacy = legacyCalls,beforePolls = consolePolls;
        touch(&s_knownSfx[i]);
        CHECK(s_knownSfx[i].iLastTimeUsed == milliseconds+1 && s_knownSfx[i].iLastLevelUsedOn == level);
        if (touch == SND_TouchSFX && mode == 0) CHECK(legacyCalls == beforeLegacy && consolePolls == beforePolls);
        result.timestamps.push_back(s_knownSfx[i].iLastTimeUsed); result.levels.push_back(s_knownSfx[i].iLastLevelUsedOn);
    }
    milliseconds = 110; evict(result,4);
    milliseconds = 111; evict(result,4);
    CHECK((result.evicted == std::vector<int>{5,7}));
    // Current-level touches survive level-cache cleanup; prior-level PCM/MP3
    // storage is reclaimed by the actual production ownership routines.
    level = 8; milliseconds = 120; touch(&s_knownSfx[1]);
    result.timestamps.push_back(s_knownSfx[1].iLastTimeUsed); result.levels.push_back(s_knownSfx[1].iLastLevelUsedOn);
    std::memset(s_channels,0,sizeof(s_channels));
    gbInsideLoadSound = qtrue; CHECK(!SND_RegisterAudio_LevelLoadEnd(qfalse)); gbInsideLoadSound = qfalse;
    CHECK(SND_RegisterAudio_LevelLoadEnd(qfalse));
    CHECK(s_knownSfx[1].bInMemory && Z_MemSize(TAG_SND_RAWDATA) == 64 && !Z_MemSize(TAG_SND_MP3STREAMHDR));
    CHECK(!SND_RegisterAudio_LevelLoadEnd(qtrue));
    level = 9; CHECK(SND_RegisterAudio_LevelLoadEnd(qfalse));
    CHECK(!s_knownSfx[1].bInMemory && !Z_MemSize(TAG_SND_RAWDATA));
    milliseconds = 125; result.delivered = drainInput();
    CHECK(result.delivered.size() == 4 && result.delivered[2].type == SE_MOUSE &&
        result.delivered[2].value == 4 && result.delivered[2].value2 == 6);
    result.recorded = journal; result.legacy = legacyCalls; result.polls = consolePolls;
    result.reads = fileReads; result.writes = fileWrites;
    CHECK(allocations.empty());
    if (mode == 2) CHECK(journalPosition == journal.size());
    return result;
}
static void sameState(const Result &before,const Result &after) {
    CHECK(before.timestamps == after.timestamps && before.levels == after.levels && before.evicted == after.evicted);
    CHECK(before.delivered == after.delivered);
}
int main() {
    const auto oldNormal = sequence(ReferenceTouch,0),newNormal = sequence(SND_TouchSFX,0);
    sameState(oldNormal,newNormal);
    CHECK(oldNormal.legacy == 4 && newNormal.legacy == 0 && newNormal.polls < oldNormal.polls);
    CHECK(!oldNormal.reads && !oldNormal.writes && !newNormal.reads && !newNormal.writes);
    const auto oldRecord = sequence(ReferenceTouch,1),newRecord = sequence(SND_TouchSFX,1);
    sameState(oldRecord,newRecord);
    CHECK(oldRecord.recorded == newRecord.recorded && oldRecord.legacy == newRecord.legacy &&
        oldRecord.polls == newRecord.polls && oldRecord.writes == newRecord.writes);
    const auto oldReplay = sequence(ReferenceTouch,2,oldRecord.recorded),newReplay = sequence(SND_TouchSFX,2,newRecord.recorded);
    sameState(oldRecord,oldReplay); sameState(oldReplay,newReplay);
    CHECK(oldReplay.legacy == newReplay.legacy && oldReplay.reads == newReplay.reads && !newReplay.polls);
    // Cached touches keep OS-console input pending until the unchanged main
    // event consumer runs; the command is delivered there with normal ownership.
    reset(0); consoleReady = true; sfx_t sound{}; SND_TouchSFX(&sound);
    CHECK(!consolePolls && consoleReady);
    auto command = drainInput(); CHECK(command.size() == 1 && command[0].type == SE_CONSOLE && command[0].payload == std::string("echo cache clock",17));
    CHECK(allocations.empty());
    // Near clock wrap, retain the existing signed timestamp values and +1
    // convention without introducing new arithmetic. INT_MAX itself was already
    // outside the old +1 expression's defined range and is not asserted here.
    for (int now : {0,INT_MAX-1,INT_MIN}) {
        reset(0); milliseconds = now; const int before = consolePolls;
        sfx_t expected; std::memcpy(&expected,&sound,sizeof(sound));
        expected.iLastTimeUsed = now+1; expected.iLastLevelUsedOn = level;
        SND_TouchSFX(&sound);
        CHECK(sound.iLastTimeUsed == now+1 && consolePolls == before && !legacyCalls);
        CHECK(!std::memcmp(&expected,&sound,sizeof(sound)));
    }
    reset(0); com_journal = nullptr; SND_TouchSFX(&sound);
    CHECK(legacyCalls == 1 && sound.iLastTimeUsed == milliseconds+1);
    reset(0);
    for (int i = 0; i < 5000; ++i) { milliseconds = 500+i; SND_TouchSFX(&sound); }
    CHECK(!legacyCalls && !consolePolls && rawCalls == 5000 && sound.iLastTimeUsed == 5500);
    std::puts("Actual sound touch/clock/cache paths: 5000 normal touches use 0 event pumps; LRU timestamps/evictions/levels and queued input preserved; record/replay bytes/calls/order, console ownership, init and signed-clock boundary checks passed.");
}
