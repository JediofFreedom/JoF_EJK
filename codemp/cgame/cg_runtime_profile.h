#ifndef EJK_CG_RUNTIME_PROFILE_H
#define EJK_CG_RUNTIME_PROFILE_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    CGP_FRAME, CGP_SETUP, CGP_CVARS, CGP_SNAPSHOTS, CGP_PREDICTION,
    CGP_VIEW, CGP_SKYPORTAL, CGP_ENTITIES, CGP_LOCAL_EFFECTS, CGP_WEAPON,
    CGP_SCHEDULED_FX, CGP_AUDIO, CGP_DRAW_ACTIVE, CGP_RENDER_SCENE,
    CGP_HUD, CGP_AUTOMAP, CGP_TAIL,
    CGP_ENTITY_LERP, CGP_ENTITY_EFFECTS, CGP_PLAYER,
    CGP_PLAYER_SETUP, CGP_PLAYER_MODEL, CGP_PLAYER_EQUIPMENT,
    CGP_PLAYER_SHADOW, CGP_PLAYER_POSE, CGP_PLAYER_BOLTS,
    CGP_PLAYER_ATTACHMENTS, CGP_PLAYER_ANIMATION, CGP_PLAYER_FORCE_FX,
    CGP_PLAYER_SABER, CGP_PLAYER_SUBMIT, CGP_PLAYER_COSMETICS,
    CGP_PLAYER_TAIL, CGP_PLAYER_TRACE,
    CGP_SHADOW_TRACE, CGP_SHADOW_MARK, CGP_WATER_CONTENTS,
    CGP_ANIM_TRIGGER, CGP_ANIM_PLAYBACK, CGP_ANIM_BONE_FRAMES, CGP_ANIM_EVENTS,
    CGP_ANIM_INDEXED, CGP_ANIM_LINEAR, CGP_ANIM_DISPATCH,
    CGP_ANIM_SOUND, CGP_ANIM_FOOTSTEP, CGP_ANIM_EFFECT, CGP_ANIM_SABER, CGP_ANIM_OTHER,
    CGP_FOOTSTEP_BOLT, CGP_FOOTSTEP_WATER, CGP_FOOTSTEP_GROUND,
    CGP_FOOTSTEP_SOUND, CGP_FOOTSTEP_FX, CGP_FOOTSTEP_MARK, CGP_COUNT
} cgProfileStage_t;
typedef struct { uint64_t started,epoch; int active; } cgProfileMark_t;
typedef void (*cgProfileOutput_t)(const char *,...);
void CG_RuntimeProfileInit(cgProfileOutput_t output);
void CG_RuntimeProfileReady(void);
void CG_RuntimeProfileShutdown(void);
void CG_RuntimeProfileReset(void);
void CG_RuntimeProfileReport(void);
cgProfileMark_t CG_RuntimeProfileFrameBegin(int enabled,int eligible);
void CG_RuntimeProfileFrameEnd(cgProfileMark_t mark,int complete,int players,int entities);
cgProfileMark_t CG_RuntimeProfileStageBegin(void);
void CG_RuntimeProfileStageEnd(cgProfileStage_t stage,cgProfileMark_t mark);
// Cgame entry points are serialized by the engine, including the joined async
// load handoff. Only the active frame thread writes this adapter fast path.
extern int cgRuntimeProfileActive;
#ifdef __cplusplus
}
#endif

static inline cgProfileMark_t CG_ProfileBegin(void) {
    cgProfileMark_t inactive = {0,0,0};
    return cgRuntimeProfileActive ? CG_RuntimeProfileStageBegin() : inactive;
}
static inline void CG_ProfileEnd(cgProfileStage_t stage,cgProfileMark_t mark) {
    if (mark.active) CG_RuntimeProfileStageEnd(stage,mark);
}
// Level 2 adds per-entity stages. Level 0/1 never read the clock here.
static inline cgProfileMark_t CG_ProfileFineBegin(void) {
    cgProfileMark_t inactive = {0,0,0};
    return cgRuntimeProfileActive >= 2 ? CG_RuntimeProfileStageBegin() : inactive;
}
typedef struct { cgProfileStage_t stage; cgProfileMark_t mark; } cgProfileScope_t;
static inline cgProfileScope_t CG_ProfileScopeBegin(cgProfileStage_t stage) {
    cgProfileScope_t scope;
    scope.stage = stage;
    scope.mark = CG_ProfileFineBegin();
    return scope;
}
static inline void CG_ProfileScopeNext(cgProfileScope_t *scope,cgProfileStage_t stage) {
    if (scope->mark.active) {
        CG_ProfileEnd(scope->stage,scope->mark);
        scope->mark = CG_ProfileFineBegin();
        scope->stage = stage;
    }
}
static inline void CG_ProfileScopeEnd(cgProfileScope_t *scope) {
    CG_ProfileEnd(scope->stage,scope->mark);
    scope->mark.active = 0;
}
#endif
