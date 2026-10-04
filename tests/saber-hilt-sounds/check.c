#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int qboolean;
typedef int sfxHandle_t;
typedef float vec3_t[3];
enum { qfalse, qtrue };
enum { MAX_CLIENTS = 32, MAX_SABERS = 2, CHAN_AUTO = 0 };
enum { WP_NONE, WP_SABER, WP_BLASTER, ET_PLAYER, ET_NPC, TEAM_FREE, TEAM_SPECTATOR };
enum { SABER_SINGLE, SABER_STAFF, SVMOD_BASEJKA, SVMOD_JAPLUS };
enum { EF_DEAD = 1, PMF_FOLLOW = 2, PERS_TEAM = 0 };
enum { JAPRO_PLUGIN_HOLSTEREDSABERS = 1, JAPRO_PLUGIN_HOLSTEREDSABER = 2 };
enum { BOTH_S1_S7 = 10, BOTH_S7_S1, BOTH_STAND1TO2, BOTH_STAND2TO1,
       BOTH_S1_S7_NEW, BOTH_S7_S1_NEW, BOTH_STAND1TO2_NEW, BOTH_STAND2TO1_NEW };

typedef struct {
    char model[64];
    int type, numBlades;
    sfxHandle_t soundOn, soundOff;
    struct { float length, desiredLength; } blade[1];
} saberInfo_t;
typedef struct { qboolean infoValid; int team; saberInfo_t saber[MAX_SABERS]; } clientInfo_t;
typedef struct {
    int number, clientNum, eType, weapon, torsoAnim, saberHolstered, eFlags;
    qboolean saberInFlight;
} entityState_t;
typedef struct {
    entityState_t currentState;
    void *ghoul2, *ghoul2weapon;
    int weapon, torsoBolt;
    qboolean saberWasInFlight, saberHiltChanged;
    vec3_t lerpOrigin;
    struct { struct { int animationTime; } torso; } pe;
} centity_t;
typedef struct { int clientNum, weapon, torsoAnim, saberHolstered, pm_flags, persistant[1]; } playerState_t;

static struct { int time, clientNum; playerState_t predictedPlayerState; } cg;
static struct { int serverMod; clientInfo_t clientinfo[MAX_CLIENTS]; } cgs;
static centity_t cg_entities[MAX_CLIENTS];
static struct { int integer; } cg_holsteredStaffSound, cp_pluginDisable;
static int weaponInstance, playerModel, copies;
static struct { int client, sound; } sounds[32];
static int soundCount;

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static void StartSound(const vec3_t origin, int client, int channel, sfxHandle_t sound)
{
    (void)origin;
    CHECK(channel == CHAN_AUTO);
    CHECK(soundCount < 32);
    sounds[soundCount].client = client;
    sounds[soundCount++].sound = sound;
}
static struct { void (*S_StartSound)(const vec3_t, int, int, sfxHandle_t); } imports = { StartSound }, *trap = &imports;

static void *CG_G2WeaponInstance(centity_t *cent, int weapon)
{
    (void)cent;
    (void)weapon;
    return &weaponInstance;
}
static void CG_CopyG2WeaponInstance(centity_t *cent, int weapon, void *model)
{
    (void)cent;
    (void)weapon;
    CHECK(model == &playerModel);
    copies++;
}
static void BG_SI_SetDesiredLength(saberInfo_t *saber, float length, int blade)
{
    (void)blade;
    saber->blade[0].desiredLength = length;
}

#include "actual.h"

static centity_t *Reset(void)
{
    centity_t *cent = &cg_entities[0];
    clientInfo_t *ci = &cgs.clientinfo[0];
    memset(&cg, 0, sizeof(cg));
    memset(&cgs, 0, sizeof(cgs));
    memset(cg_entities, 0, sizeof(cg_entities));
    memset(staffSwapSound, 0, sizeof(staffSwapSound));
    memset(staffSwapLatch, 0, sizeof(staffSwapLatch));
    soundCount = copies = 0;
    cg.time = 1000;
    cg_holsteredStaffSound.integer = 1;
    cp_pluginDisable.integer = 0;
    cgs.serverMod = SVMOD_JAPLUS;
    ci->infoValid = qtrue;
    ci->team = TEAM_FREE;
    strcpy(ci->saber[0].model, "staff");
    ci->saber[0].type = SABER_STAFF;
    ci->saber[0].numBlades = 2;
    ci->saber[0].soundOn = 11;
    ci->saber[0].soundOff = 12;
    cent->currentState.eType = ET_PLAYER;
    cent->currentState.weapon = cent->weapon = cg.predictedPlayerState.weapon = WP_SABER;
    cent->currentState.torsoAnim = cg.predictedPlayerState.torsoAnim = BOTH_S1_S7_NEW;
    cent->ghoul2 = &playerModel;
    cent->ghoul2weapon = &weaponInstance;
    return cent;
}

static void ConfirmHilt(int primary, int secondary)
{
    const qboolean changed[MAX_SABERS] = { primary, secondary };
    CG_SaberClientInfoChanged(0, qtrue, changed);
}

static void CheckWeapon(centity_t *cent)
{
    CG_CheckPlayerG2Weapons(&cg.predictedPlayerState, cent);
}

int main(void)
{
    const qboolean changed[MAX_SABERS] = { qtrue, qfalse };
    const qboolean unchanged[MAX_SABERS] = { qfalse, qfalse };
    centity_t *cent;
    int hilt, blocked;

    // A confirmed /saber change is audible even with a completed draw animation
    // and a weapon model already attached. Repeated frames must not replay it.
    cent = Reset();
    for (hilt = 1; hilt <= 4; hilt++) {
        cgs.clientinfo[0].saber[0].soundOn = 20 + hilt;
        ConfirmHilt(qtrue, qfalse);
        ConfirmHilt(qfalse, qfalse); //a second userinfo update before rendering
        CheckWeapon(cent);
        CHECK(soundCount == hilt && sounds[hilt - 1].sound == 20 + hilt);
        CHECK(sounds[hilt - 1].client == 0 && !cent->saberHiltChanged);
        CHECK(!staffSwapSound[0].sound);
        CheckWeapon(cent);
        CHECK(soundCount == hilt);
    }

    // The ordinary invalidated-model path must also bypass the old staff draw.
    cent = Reset();
    cent->weapon = WP_NONE;
    cent->ghoul2weapon = NULL;
    ConfirmHilt(qtrue, qfalse);
    CheckWeapon(cent);
    CHECK(soundCount == 1 && sounds[0].sound == 11);
    CHECK(!staffSwapSound[0].sound);

    // New hilts clear the old draw's sound and animation latch.
    cent = Reset();
    CHECK(CG_StaffSwapHoldIgnitionSound(0, 99));
    staffSwapLatch[0].swapped = qtrue;
    ConfirmHilt(qtrue, qfalse);
    CHECK(!staffSwapSound[0].sound && !staffSwapLatch[0].swapped);
    CheckWeapon(cent);
    CHECK(soundCount == 1 && sounds[0].sound == 11);

    // Name/color refreshes keep the pending ignition until the handoff.
    cent = Reset();
    CHECK(CG_StaffSwapHoldIgnitionSound(0, 11));
    ConfirmHilt(qfalse, qfalse);
    CHECK(staffSwapSound[0].sound == 11 && !cent->saberHiltChanged);
    cg.time += STAFFSWAP_SOUND_WAIT;
    CG_StaffSwapIgnitionSound(cent, STAFFSWAP_INHAND);
    CHECK(soundCount == 1 && sounds[0].sound == 11);

    // Rejected/redundant commands have no changed clientinfo and no feedback.
    cent = Reset();
    ConfirmHilt(qfalse, qfalse);
    CheckWeapon(cent);
    CHECK(soundCount == 0 && copies == 0);

    // Initial loading and another player's hilt never queue local feedback.
    cent = Reset();
    CG_SaberClientInfoChanged(0, qfalse, changed);
    CHECK(!cent->saberHiltChanged);
    cgs.clientinfo[0].infoValid = qfalse;
    CG_SaberClientInfoChanged(0, qtrue, changed);
    CHECK(!cent->saberHiltChanged);
    cgs.clientinfo[1].infoValid = qtrue;
    CG_SaberClientInfoChanged(1, qtrue, changed);
    CHECK(!cg_entities[1].saberHiltChanged);

    // Sound synchronization settings do not silence hilt-change feedback.
    cent = Reset();
    cg_holsteredStaffSound.integer = 0;
    ConfirmHilt(qtrue, qfalse);
    CheckWeapon(cent);
    CHECK(soundCount == 1);

    // Duals sound each real hilt; a removed secondary hilt is silent even if
    // its saber definition retained a default ignition handle.
    cent = Reset();
    strcpy(cgs.clientinfo[0].saber[1].model, "second hilt");
    cgs.clientinfo[0].saber[1].soundOn = 13;
    ConfirmHilt(qfalse, qtrue);
    CheckWeapon(cent);
    CHECK(soundCount == 2 && sounds[0].sound == 11 && sounds[1].sound == 13);
    cgs.clientinfo[0].saber[1].model[0] = '\0';
    ConfirmHilt(qfalse, qtrue);
    CheckWeapon(cent);
    CHECK(soundCount == 3 && sounds[2].sound == 11);

    // Ordinary draws still wait for the staff animation.
    cent = Reset();
    cent->weapon = WP_BLASTER;
    cent->ghoul2weapon = NULL;
    CheckWeapon(cent);
    CHECK(soundCount == 0 && staffSwapSound[0].sound == 11);
    cg.time += STAFFSWAP_SOUND_WAIT;
    CG_StaffSwapIgnitionSound(cent, STAFFSWAP_INHAND);
    CHECK(soundCount == 1);

    // A temporarily absent player model must not consume confirmed feedback.
    cent = Reset();
    cent->ghoul2 = NULL;
    ConfirmHilt(qtrue, qfalse);
    CheckWeapon(cent);
    CHECK(soundCount == 0 && cent->saberHiltChanged);
    cent->ghoul2 = &playerModel;
    CheckWeapon(cent);
    CHECK(soundCount == 1 && !cent->saberHiltChanged);

    // A retained return marker must not suppress a newly confirmed hilt.
    cent = Reset();
    cent->saberWasInFlight = qtrue;
    ConfirmHilt(qtrue, qfalse);
    CheckWeapon(cent);
    CHECK(soundCount == 1 && !cent->saberHiltChanged);

    // Follow, death, spectators, missing hands and flight cancel local feedback.
    for (blocked = 0; blocked < 5; blocked++) {
        cent = Reset();
        ConfirmHilt(qtrue, qfalse);
        switch (blocked) {
        case 0: cg.predictedPlayerState.pm_flags = PMF_FOLLOW; break;
        case 1: cent->currentState.eFlags = EF_DEAD; break;
        case 2: cgs.clientinfo[0].team = TEAM_SPECTATOR; break;
        case 3: cent->torsoBolt = 1; break;
        case 4: cent->currentState.saberInFlight = qtrue; break;
        }
        CheckWeapon(cent);
        CHECK(soundCount == 0 && !cent->saberHiltChanged);
    }

    // Changing a stowed hilt while carrying a gun does not ignite the saber.
    cent = Reset();
    cent->weapon = cg.predictedPlayerState.weapon = WP_BLASTER;
    ConfirmHilt(qtrue, qfalse);
    CheckWeapon(cent);
    CHECK(soundCount == 0 && !cent->saberHiltChanged);
    CG_SaberClientInfoChanged(0, qtrue, unchanged);
    CheckWeapon(cent);
    CHECK(soundCount == 0);

    puts("Saber hilt ignition checks passed.");
    return 0;
}
