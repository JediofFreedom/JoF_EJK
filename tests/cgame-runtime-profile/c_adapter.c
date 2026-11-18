#include "cg_runtime_profile.h"
int c_profile_adapter_disabled(void) {
    int i;
    for (i = 0; i < 1000; ++i) {
        cgProfileMark_t mark = CG_ProfileBegin();
        if (mark.active) return 0;
        CG_ProfileEnd(CGP_ENTITIES,mark);
    }
    return 1;
}
int c_profile_fine_adapter_disabled(void) {
    int i;
    for (i = 0; i < 1000; ++i) {
        cgProfileMark_t mark = CG_ProfileFineBegin();
        cgProfileScope_t scope = CG_ProfileScopeBegin(CGP_PLAYER_SETUP);
        if (mark.active || scope.mark.active) return 0;
        CG_ProfileEnd(CGP_PLAYER,mark);
        CG_ProfileScopeNext(&scope,CGP_PLAYER_MODEL);
        if (scope.mark.active) return 0;
        CG_ProfileScopeEnd(&scope);
    }
    return 1;
}
typedef struct { int exitAfter,visits; } centity_t;
static void CG_PlayerInternal(centity_t *cent,cgProfileScope_t *profile) {
    ++cent->visits;
    if (cent->exitAfter == 0) return;
    CG_ProfileScopeNext(profile,CGP_PLAYER_MODEL);
    ++cent->visits;
    if (cent->exitAfter == 1) return;
    CG_ProfileScopeNext(profile,CGP_PLAYER_TAIL);
    ++cent->visits;
}
// Actual production wrapper: the unchanged gameplay body can return at any
// point and the wrapper must still close both the final segment and total.
#include "player_wrapper.inc"
int c_profile_player_wrapper(int exitAfter) {
    centity_t cent = {exitAfter,0};
    CG_Player(&cent);
    return cent.visits;
}
