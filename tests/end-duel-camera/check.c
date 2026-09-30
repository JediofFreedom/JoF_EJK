#include "cgame/cg_local.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
#define DEBUGNAME(x) ((void)0)

cg_t cg;
cgs_t cgs;
vmCvar_t cp_pluginDisable, cg_duelSounds, cg_duelMusic;
int cg_dueltypes[MAX_CLIENTS];
static snapshot_t snapshot;
static cgameImport_t imports;
cgameImport_t *trap = &imports;
static int musicRestarts;

static void ClearLoopingSounds(void) {}
void CG_StartMusic(qboolean force) { CHECK(force); ++musicRestarts; }
void CG_CenterPrint(const char *text, int y, int width) { (void)text; (void)y; (void)width; }
const char *CG_GetStringEdString(char *section, char *name) { (void)section; return name; }

#include "actual.h"

static entityState_t Setup(void) {
    entityState_t event = {0};
    memset(&cg, 0, sizeof(cg));
    memset(&cgs, 0, sizeof(cgs));
    memset(&snapshot, 0, sizeof(snapshot));
    cp_pluginDisable.integer = 0;
    imports.S_ClearLoopingSounds = ClearLoopingSounds;
    musicRestarts = 0;
    cgs.serverMod = SVMOD_JAPLUS;
    cg.snap = &snapshot;
    cg.clientNum = snapshot.ps.clientNum = event.number = 3;
    cg.time = 10000;
    snapshot.ps.stats[STAT_HEALTH] = 100;
    snapshot.ps.persistant[PERS_TEAM] = TEAM_FREE;
    snapshot.ps.persistant[PERS_SPAWN_COUNT] = 7;
    cg.predictedPlayerState = snapshot.ps;
    return event;
}

int main(void) {
    entityState_t event = Setup();

    // JA+ ends the duel through EV_PRIVATE_DUEL 0, without a required obituary.
    DispatchDuelEvent(&event);
    CHECK(musicRestarts == 1);
    CHECK(cg.endDuelCameraTime == cg.time);
    CHECK(CG_EndDuelCameraActive());
    CHECK(CG_EndDuelCameraAngle() == 0.0f);
    cg.time += END_DUEL_CAMERA_DURATION / 2;
    CHECK(fabsf(CG_EndDuelCameraAngle() - 180.0f) < 0.01f);
    cg.time += END_DUEL_CAMERA_DURATION / 2;
    CHECK(!CG_EndDuelCameraActive());

    // The end event can precede prediction catching up with the duel state.
    event = Setup();
    snapshot.ps.duelInProgress = cg.predictedPlayerState.duelInProgress = qtrue;
    DispatchDuelEvent(&event);
    CHECK(CG_EndDuelCameraActive());

    event = Setup();
    cp_pluginDisable.integer = JAPRO_PLUGIN_ENDDUELROTATION;
    DispatchDuelEvent(&event);
    CHECK(cg.endDuelCameraTime == 0);
    CHECK(!CG_EndDuelCameraActive());

    event = Setup();
    snapshot.ps.stats[STAT_HEALTH] = 0;
    DispatchDuelEvent(&event);
    CHECK(!CG_EndDuelCameraActive());

    event = Setup();
    event.number = 4;
    DispatchDuelEvent(&event);
    CHECK(!CG_EndDuelCameraActive());

    event = Setup();
    snapshot.ps.clientNum = event.number = 4;
    snapshot.ps.pm_flags = PMF_FOLLOW;
    DispatchDuelEvent(&event);
    CHECK(!CG_EndDuelCameraActive());

    event = Setup();
    snapshot.ps.persistant[PERS_TEAM] = TEAM_SPECTATOR;
    DispatchDuelEvent(&event);
    CHECK(!CG_EndDuelCameraActive());

    event = Setup();
    cgs.serverMod = SVMOD_JAPRO;
    DispatchDuelEvent(&event);
    CHECK(!CG_EndDuelCameraActive());

    event = Setup();
    DispatchDuelEvent(&event);
    event.eventParm = 2;
    DispatchDuelEvent(&event);
    CHECK(!CG_EndDuelCameraActive());

    event = Setup();
    DispatchDuelEvent(&event);
    ++cg.predictedPlayerState.persistant[PERS_SPAWN_COUNT];
    CHECK(!CG_EndDuelCameraActive());

    event = Setup();
    DispatchDuelEvent(&event);
    cg.predictedPlayerState.pm_flags |= PMF_FOLLOW;
    CHECK(!CG_EndDuelCameraActive());

    event = Setup();
    DispatchDuelEvent(&event);
    cg.predictedPlayerState.persistant[PERS_TEAM] = TEAM_SPECTATOR;
    CHECK(!CG_EndDuelCameraActive());

    event = Setup();
    DispatchDuelEvent(&event);
    cg.predictedPlayerState.clientNum = 4;
    CHECK(!CG_EndDuelCameraActive());

    event = Setup();
    DispatchDuelEvent(&event);
    cg.predictedPlayerState.stats[STAT_HEALTH] = 0;
    CHECK(!CG_EndDuelCameraActive());

    event = Setup();
    DispatchDuelEvent(&event);
    cp_pluginDisable.integer = JAPRO_PLUGIN_ENDDUELROTATION;
    CHECK(!CG_EndDuelCameraActive());

    puts("PASS: JA+ duel-end event, delayed prediction, camera duration, plugin toggle, death, follow/spectator, respawn, new duel, other players/mods.");
    return 0;
}
