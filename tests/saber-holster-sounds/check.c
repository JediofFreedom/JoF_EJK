// Exercise production sound and weapon-change code with a controllable animation clock.
// Ghoul2 and audio output are mocked; this does not test rendering or live JA+ networking.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); exit(1); } } while (0)
#define assert CHECK
#define DEBUGNAME(x) ((void)0)
#define MAX_CLIENTS 4
#define MAX_WEAPONS 8
#define WP_NUM_WEAPONS MAX_WEAPONS
#define CHAN_AUTO 0
#define EF_DEAD 1
#define PMF_FOLLOW 1
#define PERS_TEAM 0
#define JAPRO_PLUGIN_HOLSTEREDSABER (1 << 9)
#define JAPRO_PLUGIN_HOLSTEREDSABERS (1 << 29)
enum { qfalse, qtrue };
enum { WP_NONE, WP_MELEE, WP_SABER };
enum { ET_PLAYER, ET_NPC };
enum { TEAM_FREE, TEAM_SPECTATOR };
enum { SVMOD_BASE, SVMOD_JAPLUS };
enum { SABER_SINGLE, SABER_STAFF };
enum { IDLE, BOTH_S1_S7, BOTH_STAND1TO2, BOTH_S1_S7_NEW, BOTH_STAND1TO2_NEW,
       BOTH_S7_S1_NEW, BOTH_STAND2TO1_NEW };
enum { EV_CHANGE_WEAPON };
enum { IGNITION = 10, SHUTDOWN, SELECT };
typedef int qboolean;
typedef int sfxHandle_t;
typedef float vec3_t[3];
typedef struct { float length, desiredLength; } blade_t;
typedef struct { char model[16]; int type, numBlades, soundOn, soundOff; blade_t blade[2]; } saberInfo_t;
typedef struct { int infoValid, team, serverSaberSoundOn[2]; saberInfo_t saber[2]; } clientInfo_t;
typedef struct {
    int number, clientNum, eType, weapon, saberHolstered, saberInFlight, eFlags;
    int torsoAnim, torsoFlip, eventParm;
} entityState_t;
typedef struct {
    int clientNum, weapon, torsoAnim, saberHolstered, pm_flags, persistant[1];
    int duelInProgress, duelIndex;
} playerState_t;
typedef struct {
    entityState_t currentState;
    int weapon, currentValid, torsoBolt, saberWasInFlight;
    int saberSoundOffDebounceTime, saberSoundOnDebounceTime;
    void *ghoul2, *ghoul2weapon;
    vec3_t lerpOrigin;
    struct { struct { int animationNumber, animationTime, lastFlip; } torso; } pe;
} centity_t;
typedef struct { sfxHandle_t selectSound; } weaponInfo_t;
static weaponInfo_t cg_weapons[MAX_WEAPONS];
static centity_t cg_entities[MAX_CLIENTS];
static struct { int time; playerState_t predictedPlayerState; } cg;
static struct {
    int serverMod, gameModels[1];
    clientInfo_t clientinfo[MAX_CLIENTS];
    struct { int selectSound; } media;
} cgs;
static struct { int integer; } cp_pluginDisable, cg_holsteredStaffSound;
static struct { float value; } cg_holsteredStaffSwap;
static int sounds[64], soundTimes[64], soundCount, boneQueries;
static float boneFrame;
static int weaponModels[MAX_WEAPONS];

static void StartSound(const float *origin, int client, int channel, int sound) {
    CHECK(soundCount < 64);
    sounds[soundCount] = sound;
    soundTimes[soundCount++] = cg.time;
}
static int BoneAnim(void *model, const char *bone, int time, float *frame, int *first,
                    int *last, int *flags, float *speed, int *models, int index) {
    ++boneQueries;
    *frame = boneFrame;
    *first = 0;
    *last = 100;
    *flags = 0;
    *speed = 1;
    return qtrue;
}
static struct {
    void (*S_StartSound)(const float *, int, int, int);
    int (*G2API_GetBoneAnim)(void *, const char *, int, float *, int *, int *, int *, float *, int *, int);
} imports = { StartSound, BoneAnim }, *trap = &imports;
static float Com_Clamp(float low, float high, float value) {
    return value < low ? low : (value > high ? high : value);
}
static float DistanceSquared(const float *a, const float *b) {
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return x*x + y*y + z*z;
}
static void *CG_G2WeaponInstance(centity_t *cent, int weapon) { return &weaponModels[weapon]; }
static void CG_CopyG2WeaponInstance(centity_t *cent, int weapon, void *model) {}
static void BG_SI_SetDesiredLength(saberInfo_t *saber, float length, int blade) {
    int i;
    for (i = 0; i < saber->numBlades; ++i) saber->blade[i].desiredLength = length;
}

#include "actual.h"

static void Reset(int staff) {
    centity_t *cent;
    clientInfo_t *ci;
    memset(&cg, 0, sizeof(cg));
    memset(&cgs, 0, sizeof(cgs));
    memset(cg_entities, 0, sizeof(cg_entities));
    memset(staffSwapSound, 0, sizeof(staffSwapSound));
    memset(staffSwapLatch, 0, sizeof(staffSwapLatch));
    soundCount = boneQueries = 0;
    cg.time = 1000;
    cgs.serverMod = SVMOD_JAPLUS;
    cgs.media.selectSound = SELECT;
    cp_pluginDisable.integer = 0;
    cg_holsteredStaffSound.integer = 1;
    cg_holsteredStaffSwap.value = 0.45f;
    cent = &cg_entities[0];
    ci = &cgs.clientinfo[0];
    cent->currentValid = qtrue;
    cent->ghoul2 = cent;
    cent->currentState.weapon = cent->weapon = cg.predictedPlayerState.weapon = WP_SABER;
    cent->ghoul2weapon = CG_G2WeaponInstance(cent, WP_SABER);
    ci->infoValid = qtrue;
    ci->saber[0].type = staff ? SABER_STAFF : SABER_SINGLE;
    ci->saber[0].numBlades = staff ? 2 : 1;
    ci->saber[0].soundOn = IGNITION;
    ci->saber[0].soundOff = SHUTDOWN;
    ci->saber[0].blade[0].length = 40;
    ci->saber[0].blade[0].desiredLength = -1;
}
static void Anim(int anim, float frame) {
    cg.predictedPlayerState.torsoAnim = cg_entities[0].currentState.torsoAnim = anim;
    cg_entities[0].pe.torso.animationNumber = anim;
    cg_entities[0].pe.torso.animationTime = cg.time;
    boneFrame = frame;
}
static void Update(float target) {
    cgs.clientinfo[0].saber[0].blade[0].desiredLength = target;
    CG_StaffSwapUpdateSounds(&cg_entities[0], &cgs.clientinfo[0]);
}
static void Event(int weapon) {
    cg_entities[0].currentState.eventParm = weapon;
    WeaponChangeEvent(&cg_entities[0]);
}
static void Commit(int weapon) {
    cg.predictedPlayerState.weapon = weapon;
    CG_CheckPlayerG2Weapons(&cg.predictedPlayerState, &cg_entities[0]);
    cg_entities[0].currentState.weapon = weapon;
}
static int Count(int sound) {
    int i, n = 0;
    for (i = 0; i < soundCount; ++i) if (sounds[i] == sound) ++n;
    return n;
}

static void TestCanceledWeaponSwitch(void) {
    Reset(0);
    Commit(WP_MELEE);
    Event(WP_SABER);
    Commit(WP_SABER);
    soundCount = 0;
    cg.time += 800; //the first completed holster's debounce has elapsed
    Event(WP_MELEE); //lowering starts, but the player reselects saber before it finishes
    CHECK(Count(SHUTDOWN) == 0);
    Commit(WP_SABER); //the twirl keeps the same weapon and blade
    CHECK(Count(SHUTDOWN) == 0 && Count(IGNITION) == 0);
    Event(WP_MELEE);
    CHECK(Count(SHUTDOWN) == 0);
    Commit(WP_MELEE); //a real switch still shuts down exactly once
    Commit(WP_MELEE);
    CHECK(Count(SHUTDOWN) == 1);
}
static void TestStaffTiming(void) {
    Reset(1);
    Anim(BOTH_S7_S1_NEW, 90);
    Update(0); //the blade starts retracting while the weapon still says saber
    CHECK(Count(SHUTDOWN) == 1 && soundTimes[0] == cg.time);
    cg.time += 50;
    Update(0);
    Commit(WP_MELEE);
    Update(0);
    CHECK(Count(SHUTDOWN) == 1);
    cgs.clientinfo[0].saber[0].blade[0].length = 0;
    cg.time += 50;
    Anim(BOTH_S1_S7_NEW, 10);
    Commit(WP_SABER); //prediction sees saber before the entity does
    CHECK(Count(IGNITION) == 0);
    Update(0);
    CHECK(Count(IGNITION) == 0);
    cg.time += 20;
    boneFrame = 46;
    Update(-1);
    CHECK(Count(IGNITION) == 1 && soundTimes[soundCount - 1] == cg.time);
    CHECK(CG_StaffSwapHoldIgnitionSound(0, IGNITION));
    CHECK(CG_StaffSwapHoldGeneralSound(cg_entities[0].lerpOrigin, IGNITION));
    Update(-1);
    CHECK(Count(IGNITION) == 1);
    cgs.clientinfo[0].saber[0].blade[0].length = 40;
    cg.time += 20;
    Anim(BOTH_S7_S1_NEW, 90);
    Update(0); //another real holster within 2 seconds must not be suppressed
    CHECK(Count(SHUTDOWN) == 2);
}
static void TestDualShutdownDebounce(void) {
    Reset(0);
    strcpy(cgs.clientinfo[0].saber[1].model, "second");
    cgs.clientinfo[0].saber[1].soundOff = SHUTDOWN + 10;
    Commit(WP_MELEE);
    CHECK(Count(SHUTDOWN) == 1 && Count(SHUTDOWN + 10) == 1);
    cg.time += 100;
    Commit(WP_SABER);
    Commit(WP_MELEE);
    CHECK(Count(SHUTDOWN) == 1 && Count(SHUTDOWN + 10) == 1);
    cg.time += 800;
    Commit(WP_SABER);
    Commit(WP_MELEE);
    CHECK(Count(SHUTDOWN) == 2 && Count(SHUTDOWN + 10) == 2);
}
static void TestMissedAnimationFrames(void) {
    Reset(1);
    cgs.clientinfo[0].saber[0].blade[0].length = 0;
    Anim(BOTH_STAND1TO2_NEW, 60); //first rendered draw frame already has the hilt in hand
    CHECK(CG_StaffSwapHoldIgnitionSound(0, IGNITION));
    Update(-1);
    CHECK(Count(IGNITION) == 1 && soundTimes[0] == cg.time); //no extra 150 ms wait
    Reset(1);
    cgs.clientinfo[0].saber[0].blade[0].length = 0;
    Anim(BOTH_S1_S7_NEW, 60);
    Update(-1); //server ignition has not arrived; the visible blade still gets its sound now
    CHECK(Count(IGNITION) == 1);
    cg.time += 100;
    CHECK(CG_StaffSwapHoldGeneralSound(cg_entities[0].lerpOrigin, IGNITION));
    Update(-1);
    CHECK(Count(IGNITION) == 1);
    Reset(1);
    Commit(WP_MELEE); //the completed weapon change precedes the JA+ reach animation
    CHECK(Count(SHUTDOWN) == 0);
    Update(0);
    CHECK(Count(SHUTDOWN) == 1);
}
static void TestCanceledDraw(void) {
    Reset(1);
    cgs.clientinfo[0].saber[0].blade[0].length = 0;
    Anim(BOTH_S1_S7_NEW, 10);
    CHECK(CG_StaffSwapHoldIgnitionSound(0, IGNITION));
    Update(0);
    Commit(WP_MELEE);
    Anim(IDLE, 0);
    Update(0);
    cg.time += 3000;
    Update(0);
    CHECK(Count(IGNITION) == 0 && Count(SHUTDOWN) == 0);
}
static void TestWeaponChangeBeforeDrawAnimation(void) {
    Reset(1);
    Commit(WP_MELEE);
    Update(0);
    cgs.clientinfo[0].saber[0].blade[0].length = 0;
    soundCount = 0;
    Anim(IDLE, 90); //weapon prediction commits before the server sends the reach
    Commit(WP_SABER);
    CHECK(Count(IGNITION) == 0);
    Update(-1);
    CHECK(Count(IGNITION) == 0);
    cg.time += 20;
    Anim(BOTH_S1_S7, 90); //plain draw still leaves the hilt on the back
    Update(0);
    CHECK(Count(IGNITION) == 0 && staffSwapSound[0].sound == IGNITION);
    cg.time += 20;
    Anim(BOTH_S1_S7_NEW, 10);
    Update(0);
    CHECK(Count(IGNITION) == 0);
    boneFrame = 46;
    Update(-1);
    CHECK(Count(IGNITION) == 1 && soundTimes[0] == cg.time);
}
static void TestStaleTorsoFrames(void) {
    const char *why = "";
    float fraction = 0;
    centity_t *cent;
    Reset(1);
    cent = &cg_entities[0];
    cent->currentState.torsoAnim = BOTH_S1_S7_NEW;
    boneFrame = 90; //outgoing idle frame would incorrectly latch the hilt in hand
    CHECK(CG_StaffSwapPhaseReal(cent, &cgs.clientinfo[0], &why, &fraction) == STAFFSWAP_ONBACK);
    CHECK(boneQueries == 0);
    Anim(BOTH_S1_S7_NEW, 10);
    CHECK(CG_StaffSwapPhaseReal(cent, &cgs.clientinfo[0], &why, &fraction) == STAFFSWAP_ONBACK);
    boneFrame = 46;
    CHECK(CG_StaffSwapPhaseReal(cent, &cgs.clientinfo[0], &why, &fraction) == STAFFSWAP_INHAND);
    boneFrame = 43; //animation blending must not move the hilt back or repeat the sound
    CHECK(CG_StaffSwapPhaseReal(cent, &cgs.clientinfo[0], &why, &fraction) == STAFFSWAP_INHAND);
    CHECK(soundCount == 0); //phase queries have no audio side effects
    cent->currentState.torsoFlip = 1; //same animation restarted but not yet applied
    CHECK(CG_StaffSwapPhaseReal(cent, &cgs.clientinfo[0], &why, &fraction) == STAFFSWAP_ONBACK);
}
static void TestResidualBladeBeforeReach(void) {
    Reset(1);
    Commit(WP_MELEE);
    Update(0);
    soundCount = 0;
    Anim(IDLE, 90);
    Commit(WP_SABER); //rapid redraw while the blade is still retracting
    Update(-1);
    CHECK(Count(IGNITION) == 0 && staffSwapSound[0].sound == IGNITION);
    CHECK(CG_StaffSwapHoldGeneralSound(cg_entities[0].lerpOrigin, IGNITION));
    cg.time += 20;
    Anim(BOTH_STAND1TO2_NEW, 10);
    Update(0);
    CHECK(Count(IGNITION) == 0);
    boneFrame = 46;
    Update(-1);
    CHECK(Count(IGNITION) == 1);
}
static void TestSingleBladeServerIgnition(void) {
    Reset(1);
    cg.predictedPlayerState.clientNum = 1; //observe a remote staff carrier
    cg_entities[0].currentState.saberHolstered = 1;
    cgs.clientinfo[0].saber[0].blade[0].length = 0;
    Anim(BOTH_STAND1TO2_NEW, 10);
    //Sound events arrive before the render update records the activation.
    CHECK(CG_StaffSwapHoldGeneralSound(cg_entities[0].lerpOrigin, IGNITION));
    Update(0);
    CHECK(Count(IGNITION) == 0);
    boneFrame = 46;
    Update(-1);
    CHECK(Count(IGNITION) == 1);
    CHECK(CG_StaffSwapHoldGeneralSound(cg_entities[0].lerpOrigin, IGNITION));
    Update(-1);
    CHECK(Count(IGNITION) == 1);

    Reset(1);
    cg_entities[0].currentState.saberHolstered = 2; //prediction is ahead of the snapshot
    cg.predictedPlayerState.saberHolstered = 1;
    cg.predictedPlayerState.torsoAnim = BOTH_STAND1TO2_NEW;
    CHECK(CG_StaffSwapHoldGeneralSound(cg_entities[0].lerpOrigin, IGNITION));
}
static void TestGatingAndReset(void) {
    Reset(1);
    cg_holsteredStaffSound.integer = 0;
    Commit(WP_MELEE);
    CHECK(Count(SHUTDOWN) == 1);
    Reset(1);
    cp_pluginDisable.integer = JAPRO_PLUGIN_HOLSTEREDSABER;
    CHECK(!CG_StaffSwapHoldShutdownSound(0));
    Reset(0);
    CHECK(!CG_StaffSwapHoldShutdownSound(0));
    Reset(1);
    Anim(BOTH_S1_S7_NEW, 10);
    CHECK(CG_StaffSwapHoldIgnitionSound(0, IGNITION));
    cg_entities[0].currentState.eFlags = EF_DEAD;
    Update(0);
    CHECK(!staffSwapSound[0].sound && soundCount == 0);
    CG_StaffSwapForgetClient(0);
    CHECK(!staffSwapLatch[0].swapped);
}
int main(void) {
    TestCanceledWeaponSwitch();
    TestStaffTiming();
    TestDualShutdownDebounce();
    TestMissedAnimationFrames();
    TestCanceledDraw();
    TestWeaponChangeBeforeDrawAnimation();
    TestStaleTorsoFrames();
    TestResidualBladeBeforeReach();
    TestSingleBladeServerIgnition();
    TestGatingAndReset();
    puts("Saber holster sound regression checks passed.");
    return 0;
}
