#include "../../codemp/qcommon/q_shared.h"
#include "../../codemp/game/bg_public.h"
#include "../../codemp/game/anims.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static int randomChoice, customEntity, customCalls, registrations;
static char customName[MAX_QPATH], registeredName[MAX_QPATH];
static int transitions, damageFeedback, painFeedback;
typedef struct { playerState_t ps; } testSnapshot_t;
static testSnapshot_t serverSnapshot;
static struct {
    playerState_t predictedPlayerState;
    testSnapshot_t *snap;
} cg;

void QDECL Com_Printf(const char *format, ...) { (void)format; }
void QDECL Com_Error(int code, const char *format, ...) { (void)code; (void)format; exit(2); }
void *BG_Alloc(int size) { void *p = calloc(1, size); CHECK(p); return p; }
int Q_irand(int low, int high) { CHECK(high >= low); return randomChoice ? high : low; }

static sfxHandle_t RegisterSound(const char *name) {
    CHECK(name[0] != '*');
    Q_strncpyz(registeredName, name, sizeof(registeredName));
    return 100 + ++registrations;
}
static int RegisterEffect(const char *name) { (void)name; return 1; }
static struct {
    sfxHandle_t (*S_RegisterSound)(const char *);
    int (*FX_RegisterEffect)(const char *);
} imports = { RegisterSound, RegisterEffect }, *trap = &imports;

static sfxHandle_t CG_CustomSound(int entityNum, const char *name) {
    customEntity = entityNum;
    customCalls++;
    Q_strncpyz(customName, name, sizeof(customName));
    return entityNum + 1000;
}
static void CG_InterpolatePlayerState(qboolean grabAngles) {
    CHECK(!grabAngles);
    cg.predictedPlayerState = cg.snap->ps;
}
static void CG_TransitionPlayerState(playerState_t *ps, playerState_t *old) {
    transitions++;
    if (ps->damageEvent != old->damageEvent && ps->damageCount) damageFeedback++;
    if (ps->stats[STAT_HEALTH] < old->stats[STAT_HEALTH] - 3) painFeedback++;
}

static stringID_table_t animTable[] = {
    ENUM2STRING(BOTH_KYLE_PA_2), ENUM2STRING(BOTH_KYLE_PA_3),
    ENUM2STRING(BOTH_PLAYER_PA_2), ENUM2STRING(BOTH_PLAYER_PA_3),
    ENUM2STRING(BOTH_JUMP1), {NULL, -1}
};
#include "actual.h"

static animevent_t events[MAX_ANIM_EVENTS];
static animation_t animations[MAX_TOTALANIMATIONS];

static void Parse(const char *text) {
    int i = 0;
    memset(events, 0, sizeof(events));
    ParseAnimationEvtBlock("test", events, animations, &i, &text);
}

int main(void) {
    int i, calls;
    for (i = 0; i < MAX_TOTALANIMATIONS; i++) {
        animations[i].firstFrame = i;
        animations[i].numFrames = 200;
    }

    // One shared animation must use the voice of each actor at playback.
    Parse("{ BOTH_PLAYER_PA_2 AEV_SOUNDCHAN 35 CHAN_VOICE *gasp.mp3 0 0 0 }");
    CHECK(registrations == 0);
    CHECK(events[0].eventData[AED_SOUNDCHANNEL] == CHAN_VOICE);
    CHECK(CG_AnimEventSound(3, &events[0]) == 1003);
    CHECK(customEntity == 3 && !strcmp(customName, "*gasp.mp3"));
    CHECK(CG_AnimEventSound(70, &events[0]) == 1070);
    CHECK(customEntity == 70 && !strcmp(customName, "*gasp.mp3"));

    // Random custom variants retain their lower bound, including a single variant.
    Parse("{ BOTH_PLAYER_PA_3 AEV_SOUND 35 *choke%d.mp3 2 3 75 }");
    CHECK(events[0].eventData[AED_SOUND_PROBABILITY] == 75);
    randomChoice = 0;
    CG_AnimEventSound(3, &events[0]);
    CHECK(!strcmp(customName, "*choke2.mp3"));
    randomChoice = 1;
    CG_AnimEventSound(3, &events[0]);
    CHECK(!strcmp(customName, "*choke3.mp3"));
    Parse("{ BOTH_KYLE_PA_3 AEV_SOUND 29 *taunt%d.mp3 2 2 0 }");
    CG_AnimEventSound(70, &events[0]);
    CHECK(!strcmp(customName, "*taunt2.mp3"));

    // Ordinary impacts still use registered sounds, not custom voice lookup.
    calls = customCalls;
    Parse("{ BOTH_KYLE_PA_2 AEV_SOUND 32 sound/weapons/melee/punch%d.mp3 1 4 0 }");
    CHECK(registrations == 4);
    CHECK(CG_AnimEventSound(70, &events[0]) == 104);
    CHECK(customCalls == calls);

    // An overriding impact must clear a previous custom name on the same frame.
    Parse("{ BOTH_PLAYER_PA_2 AEV_SOUND 47 *pain50.mp3 0 0 0\n"
          "BOTH_PLAYER_PA_2 AEV_SOUND 47 sound/player/fallsplat.wav 0 0 0 }");
    CHECK(CG_AnimEventSound(3, &events[0]) == 105);
    CHECK(!events[0].stringData && customCalls == calls);

    // Unrelated animations keep their existing gameplay-driven voice behavior.
    Parse("{ BOTH_JUMP1 AEV_SOUND 1 *jump1.mp3 0 0 0 }");
    CHECK(CG_AnimEventSound(3, &events[0]) == 0);
    Parse("{ BOTH_JUMP1 AEV_SOUND 1 *jump%d.mp3 1 3 0 }");
    CHECK(CG_AnimEventSound(3, &events[0]) == 0);
    CHECK(customCalls == calls);

    // A held local player must get each damage transition once, even while
    // movement prediction is bypassed and many render frames share a snapshot.
    cg.snap = &serverSnapshot;
    cg.predictedPlayerState.stats[STAT_HEALTH] = 100;
    serverSnapshot.ps = cg.predictedPlayerState;
    serverSnapshot.ps.legsAnim = BOTH_PLAYER_PA_2;
    serverSnapshot.ps.legsTimer = 3000;
    serverSnapshot.ps.stats[STAT_HEALTH] = 80;
    serverSnapshot.ps.damageEvent = 1;
    serverSnapshot.ps.damageCount = 20;
    PredictHold();
    CHECK(transitions == 1 && damageFeedback == 1 && painFeedback == 1);
    PredictHold();
    PredictHold();
    CHECK(damageFeedback == 1 && painFeedback == 1);
    serverSnapshot.ps.stats[STAT_HEALTH] = 50;
    serverSnapshot.ps.damageEvent++;
    PredictHold();
    CHECK(damageFeedback == 2 && painFeedback == 2);
    calls = transitions;
    serverSnapshot.ps.legsAnim = BOTH_PLAYER_PA_3_FLY;
    PredictHold();
    CHECK(transitions == calls); // Flight resumes normal prediction.

    puts("kata sound checks passed");
    return 0;
}
