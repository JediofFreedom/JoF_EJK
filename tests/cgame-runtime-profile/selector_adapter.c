#include "cgame/cg_local.h"
#include "cgame/cg_runtime_profile.h"
#include "game/bg_anim_events.h"
#include <stdlib.h>
#include <string.h>

centity_t cg_entities[MAX_GENTITIES];
bgLoadedAnim_t bgAllAnims[MAX_ANIM_FILES];
bgLoadedEvents_t bgAllEvents[MAX_ANIM_FILES];
static animation_t selectorAnimations[MAX_TOTALANIMATIONS];
static int dispatches;
int Q_irand(int low,int high) { abort(); } // MOVE-only fixture never requests RNG.
static void ProfileDispatch(centity_t *cent,animevent_t *event) {
    ++dispatches;
}
#include "selector_grapple.inc"
#include "anim_selector.inc"

int c_profile_selector_adapter(void) {
    bgLoadedEvents_t *events = &bgAllEvents[0];
    centity_t *cent = &cg_entities[0];
    memset(events,0,sizeof(*events)); memset(cent,0,sizeof(*cent));
    memset(selectorAnimations,0,sizeof(selectorAnimations));
    bgAllAnims[0].anims = selectorAnimations;
    cent->currentState.torsoAnim = cent->nextState.torsoAnim = BOTH_STAND1;
    selectorAnimations[BOTH_STAND1].numFrames = 32;
    selectorAnimations[BOTH_STAND1].frameLerp = 50;
    selectorAnimations[BOTH_KYLE_PA_2] = selectorAnimations[BOTH_STAND1];
    events->torsoAnimEvents[0].eventType = AEV_MOVE;
    events->torsoAnimEvents[0].keyFrame = 10;
    events->torsoAnimEvents[1].eventType = AEV_MOVE;
    events->torsoAnimEvents[1].keyFrame = 10;
    BG_BuildAnimEventIndexes(events);
    dispatches = 0;
    ProfileActualPlayerAnimEvents(0,0,qtrue,9,10,0); // Indexed, two dispatches.
    if (dispatches != 2) return 0;
    events->torsoIndex.valid = qfalse;
    ProfileActualPlayerAnimEvents(0,0,qtrue,10,11,0); // Invalid index, no match.
    if (dispatches != 2) return 0;
    BG_BuildAnimEventIndexes(events);
    cent->currentState.torsoAnim = cent->nextState.torsoAnim = BOTH_KYLE_PA_2;
    ProfileActualPlayerAnimEvents(0,0,qtrue,8,12,0); // Grapple fallback, two matches.
    if (dispatches != 4) return 0;
    events->torsoAnimEvents[0].eventType = AEV_NONE;
    BG_BuildAnimEventIndexes(events);
    cent->currentState.torsoAnim = cent->nextState.torsoAnim = BOTH_STAND1;
    ProfileActualPlayerAnimEvents(0,0,qtrue,9,10,0); // Empty indexed catalogue.
    return dispatches == 4;
}
