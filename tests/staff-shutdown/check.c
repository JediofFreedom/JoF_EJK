#include <stdio.h>
#include <string.h>

#define MAX_CLIENTS 32
#define BOTH_S7_S1_NEW 1
#define BOTH_STAND2TO1_NEW 2
#define CHAN_AUTO 0
typedef enum { STAFFSWAP_NONE, STAFFSWAP_ONBACK } staffSwapPhase_t;
typedef struct {
    struct { int clientNum, torsoAnim, saberHolstered; } currentState;
    struct { struct { int animationTime; } torso; } pe;
    float lerpOrigin[3];
} centity_t;
typedef struct {
    struct { int soundOff; struct { float length; } blade[1]; } saber[1];
} clientInfo_t;
typedef struct { int offAnim, offAnimTime, offTime; } staffSwapSound_t;
static staffSwapSound_t staffSwapSound[MAX_CLIENTS];
static struct { int integer; } cg_holsteredStaffSound = {1};
static struct { int time; } cg = {100};
static int sounds;
static void startSound(const float *origin, int client, int channel, int sound) {
    (void)origin; (void)client; (void)channel; (void)sound;
    sounds++;
}
static struct { void (*S_StartSound)(const float *, int, int, int); } traps = {startSound}, *trap = &traps;
#include "actual.h"

int main(void) {
    centity_t cent = {0};
    clientInfo_t ci = {0};
    int anim, holstered;
    ci.saber[0].soundOff = 1;
    ci.saber[0].blade[0].length = 20;
    cent.pe.torso.animationTime = 50;
    for (anim = BOTH_S7_S1_NEW; anim <= BOTH_STAND2TO1_NEW; anim++) {
        for (holstered = 0; holstered <= 2; holstered++) {
            memset(staffSwapSound, 0, sizeof(staffSwapSound));
            cent.currentState.torsoAnim = anim;
            cent.currentState.saberHolstered = holstered;
            sounds = 0;
            CG_StaffSwapShutdownSound(&cent, &ci, STAFFSWAP_ONBACK);
            CG_StaffSwapShutdownSound(&cent, &ci, STAFFSWAP_ONBACK);
            if (sounds != (holstered == 2 ? 0 : 1)) {
                fprintf(stderr, "anim %d, holstered %d: %d shutdown sounds\n", anim, holstered, sounds);
                return 1;
            }
            if (holstered == 2 && staffSwapSound[0].offTime) return 1;
        }
    }
    memset(staffSwapSound, 0, sizeof(staffSwapSound));
    cent.currentState.saberHolstered = 0;
    ci.saber[0].blade[0].length = 0;
    sounds = 0;
    CG_StaffSwapShutdownSound(&cent, &ci, STAFFSWAP_ONBACK);
    if (sounds) return 1;
    puts("Staff shutdown sound checks passed");
    return 0;
}
