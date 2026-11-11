#include "cgame/cg_local.h"
#include "game/bg_anim_events.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(v) do { if (!(v)) { fprintf(stderr,"line %d: %s\n",__LINE__,#v); exit(1); } } while (0)

centity_t cg_entities[MAX_GENTITIES];
bgLoadedAnim_t bgAllAnims[MAX_ANIM_FILES];
bgLoadedEvents_t bgAllEvents[MAX_ANIM_FILES];
int bgNumAnimEvents = 1;
qboolean BGPAFtextLoaded;
static int bg_animParseIncluding;
static cgameImport_t imports;
cgameImport_t *trap = &imports;
static unsigned eventVisits;
static uint32_t randomState;
static animevent_t *activeEvents;
static char eventStrings[MAX_ANIM_EVENTS][8];
typedef struct { int slot,type,key,data[AED_ARRAY_SIZE]; } Dispatch;
typedef struct { int low,high,result; } RandomCall;
typedef struct { int dispatches,randoms; Dispatch dispatch[MAX_ANIM_EVENTS]; RandomCall random[1024]; } Log;
static Log logData;
static uint32_t nextRandom(void) { randomState = randomState*UINT32_C(1664525)+UINT32_C(1013904223); return randomState; }
int Q_irand(int low,int high) {
    RandomCall *call;
    CHECK(high >= low && logData.randoms < 1024);
    call = &logData.random[logData.randoms++];
    call->low = low; call->high = high;
    return call->result = low+(int)(nextRandom()%(unsigned)(high-low+1));
}
void CG_PlayerAnimEventDo(centity_t *cent,animevent_t *event) {
    Dispatch *call;
    int i;
    CHECK(cent == &cg_entities[3] && logData.dispatches < MAX_ANIM_EVENTS);
    call = &logData.dispatch[logData.dispatches++];
    call->slot = (int)(event-activeEvents); call->type = event->eventType; call->key = event->keyFrame;
    for (i = 0; i < AED_ARRAY_SIZE; ++i) call->data[i] = event->eventData[i];
    // Model the stateful work nested inside dispatch: sound variant RNG and
    // deferred effect metadata. Neither changes an indexed key or event type.
    if (event->eventType == AEV_SOUND || event->eventType == AEV_SOUNDCHAN ||
        event->eventType == AEV_SABER_SWING || event->eventType == AEV_SABER_SPIN) Q_irand(0,3);
    if (event->eventType == AEV_EFFECT) {
        ++event->eventData[AED_BOLTINDEX];
        if (event->stringData) event->stringData[0] = 0;
    }
}
static void QDECL TestPrint(const char *format,...) { (void)format; }
static void NORETURN QDECL TestError(int code,const char *format,...) { (void)code; (void)format; exit(2); }
void (*Com_Printf)(const char *,...) = TestPrint;
NORETURN_PTR void (*Com_Error)(int,const char *,...) = TestError;
static void *allocations[1024];
static int allocationCount;
void *BG_Alloc(int bytes) {
    CHECK(allocationCount < 1024);
    allocations[allocationCount] = calloc(1,bytes);
    CHECK(allocations[allocationCount]);
    return allocations[allocationCount++];
}
static stringID_table_t animTable[] = {
    ENUM2STRING(BOTH_STAND1), ENUM2STRING(BOTH_STAND2), ENUM2STRING(BOTH_RUN1),
    ENUM2STRING(BOTH_KYLE_PA_2), {NULL,-1}
};
static animation_t animations[MAX_TOTALANIMATIONS];
#include "tables.h"
#include "services.h"
#define CG_PlayerAnimEvents ReferenceEvents
#include "reference-instrumented.h"
#undef CG_PlayerAnimEvents
#include "subject.h"

typedef void (*Selector)(int,int,qboolean,int,int,int);
static uint64_t scenarios,referenceVisits,subjectVisits,dispatches;
static void compare(bgLoadedEvents_t input,qboolean torso,int oldFrame,int frame,uint32_t seed,int requireLess) {
    bgLoadedEvents_t oldResult;
    Log oldLog;
    char initialStrings[MAX_ANIM_EVENTS][8],oldStrings[MAX_ANIM_EVENTS][8];
    uint32_t oldRandom;
    unsigned oldVisits;
    int i;
    for (i = 0; i < MAX_ANIM_EVENTS; ++i) {
        memcpy(initialStrings[i],"*flash",7); initialStrings[i][7] = 0;
        input.torsoAnimEvents[i].stringData = eventStrings[i];
        input.legsAnimEvents[i].stringData = eventStrings[i];
    }
    bgAllEvents[7] = input;
    activeEvents = torso ? bgAllEvents[7].torsoAnimEvents : bgAllEvents[7].legsAnimEvents;
    memcpy(eventStrings,initialStrings,sizeof(eventStrings));
    memset(&logData,0,sizeof(logData)); randomState = seed; eventVisits = 0;
    ReferenceEvents(0,7,torso,oldFrame,frame,3);
    oldResult = bgAllEvents[7]; oldLog = logData; oldRandom = randomState; oldVisits = eventVisits;
    memcpy(oldStrings,eventStrings,sizeof(oldStrings));
    bgAllEvents[7] = input;
    activeEvents = torso ? bgAllEvents[7].torsoAnimEvents : bgAllEvents[7].legsAnimEvents;
    memcpy(eventStrings,initialStrings,sizeof(eventStrings));
    memset(&logData,0,sizeof(logData)); randomState = seed; eventVisits = 0;
    CG_PlayerAnimEvents(0,7,torso,oldFrame,frame,3);
    CHECK(!memcmp(&oldResult,&bgAllEvents[7],sizeof(oldResult)));
    CHECK(!memcmp(&oldLog,&logData,sizeof(oldLog)) && oldRandom == randomState);
    CHECK(!memcmp(oldStrings,eventStrings,sizeof(oldStrings)));
    CHECK(eventVisits <= oldVisits);
    if (requireLess) CHECK(eventVisits < oldVisits);
    referenceVisits += oldVisits; subjectVisits += eventVisits; dispatches += logData.dispatches; ++scenarios;
}
static void initializeAnimations(void) {
    int i;
    memset(animations,0,sizeof(animations));
    for (i = 0; i < MAX_TOTALANIMATIONS; ++i) {
        animations[i].firstFrame = 100; animations[i].numFrames = 100;
        animations[i].frameLerp = 50; animations[i].loopFrames = -1;
    }
    bgAllAnims[0].anims = animations;
    cg_entities[3].currentState.number = 3;
    cg_entities[3].currentState.legsAnim = cg_entities[3].currentState.torsoAnim = BOTH_STAND1;
    cg_entities[3].nextState.legsAnim = cg_entities[3].nextState.torsoAnim = BOTH_STAND1;
}
static void testSelectors(void) {
    bgLoadedEvents_t input;
    int i,trial;
    memset(&input,0,sizeof(input));
    for (i = 0; i < MAX_ANIM_EVENTS; ++i) {
        input.torsoAnimEvents[i].eventType = input.legsAnimEvents[i].eventType = (animEventType_t)(1+i%9);
        input.torsoAnimEvents[i].keyFrame = input.legsAnimEvents[i].keyFrame = (unsigned short)((i*197)%65536);
        input.torsoAnimEvents[i].eventData[AED_SOUND_PROBABILITY] = input.legsAnimEvents[i].eventData[AED_SOUND_PROBABILITY] = (short)(i%110);
    }
    BG_BuildAnimEventIndexes(&input);
    compare(input,qtrue,198,199,1,1); compare(input,qfalse,1,394,2,1);
    compare(input,qtrue,65534,65535,3,1); compare(input,qfalse,2147483643,2147483645,4,1);
    // Equal keys in unsorted source slots and all legacy type/probability paths.
    for (i = 0; i < MAX_ANIM_EVENTS; ++i) input.torsoAnimEvents[i].keyFrame = 150;
    input.torsoAnimEvents[0].eventType = AEV_MOVE;
    input.torsoAnimEvents[1].eventType = (animEventType_t)99; // Preserve doEvent carry to default.
    BG_BuildAnimEventIndexes(&input); compare(input,qtrue,149,150,5,0);
    CHECK(logData.dispatches == MAX_ANIM_EVENTS);
    input.torsoAnimEvents[120].eventType = AEV_NONE;
    BG_BuildAnimEventIndexes(&input); compare(input,qtrue,149,150,6,1);
    CHECK(logData.dispatches == 120);
    // Invalidation retains the complete original scan until parsing rebuilds.
    input.torsoIndex.valid = qfalse; compare(input,qtrue,149,150,7,0);
    CHECK(eventVisits == 121);
    for (trial = 0; trial < 8000; ++trial) {
        int count,oldFrame,frame,animation = trial%5 == 0 ? BOTH_KYLE_PA_2 : BOTH_STAND1;
        memset(&input,0,sizeof(input));
        randomState = (uint32_t)trial+9;
        count = (int)(nextRandom()%(MAX_ANIM_EVENTS+1));
        for (i = 0; i < count; ++i) {
            animevent_t event;
            int j;
            memset(&event,0,sizeof(event));
            event.eventType = (animEventType_t)(1+nextRandom()%10);
            event.keyFrame = (unsigned short)(nextRandom()%350);
            for (j = 0; j < AED_ARRAY_SIZE; ++j) event.eventData[j] = (short)(nextRandom()%130);
            input.torsoAnimEvents[i] = input.legsAnimEvents[i] = event;
        }
        if (count > 4 && trial%7 == 0) input.torsoAnimEvents[count/2].eventType = AEV_NONE;
        BG_BuildAnimEventIndexes(&input);
        cg_entities[3].currentState.legsAnim = cg_entities[3].currentState.torsoAnim = animation;
        cg_entities[3].nextState.legsAnim = cg_entities[3].nextState.torsoAnim = trial%6 == 0 ? BOTH_STAND2 : animation;
        animations[animation].frameLerp = trial%2 ? 50 : -50;
        animations[animation].loopFrames = trial%3 ? 0 : -1;
        oldFrame = (int)(nextRandom()%300); frame = (int)(nextRandom()%300);
        compare(input,trial%2 ? qtrue : qfalse,oldFrame,frame,(uint32_t)trial*97+1,0);
    }
    CHECK(subjectVisits*4 < referenceVisits && dispatches > 1000);
}

typedef struct { const char *path,*text; } File;
static File files[8];
static int fileCount,fileOpens;
static int Open(const char *path,fileHandle_t *file,fsMode_t mode) {
    int i;
    CHECK(mode == FS_READ); ++fileOpens;
    for (i = 0; i < fileCount; ++i) if (!strcmp(files[i].path,path)) {
        *file = i+1; return (int)strlen(files[i].text);
    }
    *file = 0; return -1;
}
static int Read(void *buffer,int length,fileHandle_t file) {
    CHECK(file > 0 && file <= fileCount);
    memcpy(buffer,files[file-1].text,length); return length;
}
static void Close(fileHandle_t file) { CHECK(file > 0 && file <= fileCount); }
static sfxHandle_t Sound(const char *name) { CHECK(name && name[0]); return 11; }
static int Effect(const char *name) { CHECK(name && name[0]); return 12; }
static void testLifecycle(void) {
    bgLoadedEvents_t retained;
    int opens,index,slot;
    const char *direct = "{ BOTH_STAND1 AEV_MOVE 61 1 2 3 }";
    int unused = 0;
    memset(bgAllEvents,0,sizeof(bgAllEvents)); bgNumAnimEvents = 1;
    initializeAnimations();
    imports.FS_Open = Open; imports.FS_Read = Read; imports.FS_Close = Close;
    imports.S_RegisterSound = Sound; imports.FX_RegisterEffect = Effect;
    files[0].path = "models/players/root/animevents.cfg";
    files[0].text = "include child UPPEREVENTS { BOTH_STAND1 AEV_SOUND 50 sound/test 0 0 100 BOTH_STAND1 AEV_EFFECT 52 effect/child none 100 BOTH_STAND1 AEV_MOVE 51 1 2 3 } LOWEREVENTS { BOTH_STAND2 AEV_FIRE 10 0 100 }";
    files[1].path = "models/players/child/animevents.cfg";
    files[1].text = "UPPEREVENTS { BOTH_STAND1 AEV_SOUND 50 sound/child 0 0 50 BOTH_STAND1 AEV_EFFECT 52 effect/child none 100 }";
    fileCount = 2; fileOpens = 0;
    CHECK(BG_ParseAnimationEvtFile("models/players/root/",0,1) == 1 && fileOpens == 2);
    CHECK(bgAllEvents[1].eventsParsed && bgAllEvents[1].torsoIndex.valid && bgAllEvents[1].legsIndex.valid);
    CHECK(bgAllEvents[1].torsoIndex.count == 3 && bgAllEvents[1].legsIndex.count == 1);
    slot = CheckAnimFrameForEventType(bgAllEvents[1].torsoAnimEvents,150,AEV_SOUND);
    CHECK(slot >= 0 && bgAllEvents[1].torsoAnimEvents[slot].eventData[AED_SOUND_PROBABILITY] == 100); // Include stomp.
    retained = bgAllEvents[1]; opens = fileOpens;
    CHECK(BG_ParseAnimationEvtFile("models/players/root/",0,1) == 1 && fileOpens == opens);
    CHECK(!memcmp(&retained,&bgAllEvents[1],sizeof(retained)));
    // Direct block mutations invalidate the owning array before writes. Runtime
    // playback remains correct through the linear fallback until a parser hit.
    ParseAnimationEvtBlock("direct",bgAllEvents[1].torsoAnimEvents,animations,&unused,&direct);
    CHECK(!bgAllEvents[1].torsoIndex.valid);
    CHECK(BG_ParseAnimationEvtFile("models/players/root/",0,1) == 1 && bgAllEvents[1].torsoIndex.valid);
    CHECK(fileOpens == opens);
    // Filename reuse from a different requested slot rebuilds an invalid index.
    bgAllEvents[1].legsIndex.valid = qfalse;
    CHECK(BG_ParseAnimationEvtFile("models/players/root/",0,2) == 1 && bgAllEvents[1].legsIndex.valid);
    CHECK(fileOpens == opens);
    // Missing files are cached as empty catalogues, never retaining prior keys.
    bgAllEvents[2] = retained; bgAllEvents[2].eventsParsed = qfalse;
    CHECK(BG_ParseAnimationEvtFile("models/players/missing/",0,2) == -1);
    CHECK(bgAllEvents[2].eventsParsed && bgAllEvents[2].torsoIndex.valid && !bgAllEvents[2].torsoIndex.count);
    CHECK(bgAllEvents[2].legsIndex.valid && !bgAllEvents[2].legsIndex.count);
    // Explicit cgame reset invalidates every model set. Retained cached event
    // data can rebuild independently without interpreting stale animation data.
    BG_InitAnimsets();
    for (index = 0; index < MAX_ANIM_FILES; ++index)
        CHECK(!bgAllEvents[index].torsoIndex.valid && !bgAllEvents[index].legsIndex.valid);
    opens = fileOpens;
    CHECK(BG_ParseAnimationEvtFile("models/players/root/",0,1) == 1 && fileOpens == opens);
    CHECK(bgAllEvents[1].torsoIndex.valid && bgAllEvents[1].legsIndex.valid);
    initializeAnimations();
    // Force a reload at the same address with fewer, differently ordered keys.
    bgAllEvents[1].eventsParsed = qfalse;
    bgAllEvents[1].filename[0] = 0;
    files[0].text = "UPPEREVENTS { BOTH_STAND2 AEV_MOVE 4 1 2 3 }";
    CHECK(BG_ParseAnimationEvtFile("models/players/root/",0,1) == 1);
    CHECK(bgAllEvents[1].torsoIndex.count == 1 && bgAllEvents[1].torsoAnimEvents[0].keyFrame == 104);
    CHECK(!bgAllEvents[1].legsIndex.count);
}
int main(void) {
    int i;
    initializeAnimations(); testSelectors(); testLifecycle();
    for (i = 0; i < allocationCount; ++i) free(allocations[i]);
    printf("Animation events: %llu exact RNG/order/mutation scenarios, %llu dispatches; visited slots %llu -> %llu; parser include/stomp/cache/reset/reload checks passed.\n",
        (unsigned long long)scenarios,(unsigned long long)dispatches,
        (unsigned long long)referenceVisits,(unsigned long long)subjectVisits);
    return 0;
}
