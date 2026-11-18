#include "cgame/cg_local.h"
#include "cg_runtime_profile.h"
#include <string.h>

cg_t cg;
cgs_t cgs;
static cgameImport_t imports;
cgameImport_t *trap = &imports;
static int boneCalls,eventCalls,playbackCalls,failBones;
static float rootFrame,lumbarFrame;
static qboolean GetBoneFrame(void *model,const char *bone,const int time,float *frame,int *models,const int index) {
    ++boneCalls;
    if (failBones) return qfalse;
    *frame = !strcmp(bone,"model_root") ? rootFrame : lumbarFrame;
    return qtrue;
}
void CG_PlayerAnimEvents(int anim,int event,qboolean torso,int oldFrame,int frame,int entity) {
    ++eventCalls;
}
#include "anim_trigger.inc"
static void CG_PlayerAnimation(centity_t *cent,int *oldLegs,int *legs,float *legsLerp,int *oldTorso,int *torso,float *torsoLerp) {
    ++playbackCalls;
    *oldLegs = cent->pe.legs.oldFrame; *legs = cent->pe.legs.frame; *legsLerp = cent->pe.legs.backlerp;
    *oldTorso = cent->pe.torso.oldFrame; *torso = cent->pe.torso.frame; *torsoLerp = cent->pe.torso.backlerp;
}
static int RunCallSite(centity_t *cent) {
    refEntity_t legs,torso;
    cgProfileScope_t scope = CG_ProfileScopeBegin(CGP_PLAYER_SETUP);
    cgProfileScope_t *profile = &scope;
    memset(&legs,0,sizeof(legs)); memset(&torso,0,sizeof(torso));
#include "anim_callsite.inc"
    CG_ProfileScopeEnd(&scope);
    return legs.frame == cent->pe.legs.frame && torso.frame == cent->pe.torso.frame;
}
int c_profile_animation_adapter(void) {
    centity_t cent;
    memset(&cent,0,sizeof(cent)); memset(&imports,0,sizeof(imports));
    imports.G2API_GetBoneFrame = GetBoneFrame;
    boneCalls = eventCalls = playbackCalls = failBones = 0;
    rootFrame = 10.25f; lumbarFrame = 11.75f;
    if (!RunCallSite(&cent) || cent.pe.legs.frame != 10 || cent.pe.torso.frame != 11 ||
        cent.pe.torso.backlerp != .25f || boneCalls != 2 || eventCalls != 2) return 0;
    if (!RunCallSite(&cent) || cent.pe.legs.oldFrame != 10 || cent.pe.torso.oldFrame != 11 ||
        boneCalls != 4 || eventCalls != 2) return 0; // Same sampled frames skip event scans.
    cent.noLumbar = qtrue; rootFrame = 20.5f;
    if (!RunCallSite(&cent) || cent.pe.legs.frame != 20 || cent.pe.torso.frame != 20 ||
        cent.pe.torso.oldFrame != 10 || boneCalls != 5 || eventCalls != 3) return 0;
    cent.noLumbar = qfalse; failBones = 1;
    if (!RunCallSite(&cent) || cent.pe.legs.frame || cent.pe.torso.frame ||
        cent.pe.torso.backlerp != 1 || boneCalls != 7 || eventCalls != 5 || playbackCalls != 4) return 0;
    return 1;
}
