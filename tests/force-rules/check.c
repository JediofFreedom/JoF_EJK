#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); exit(1); } } while (0)
typedef int qboolean;
enum { qfalse, qtrue };
enum { FP_HEAL, FP_LEVITATION, FP_SPEED, FP_PUSH, FP_PULL, FP_TELEPATHY,
    FP_GRIP, FP_LIGHTNING, FP_RAGE, FP_PROTECT, FP_ABSORB, FP_TEAM_HEAL,
    FP_TEAM_FORCE, FP_DRAIN, FP_SEE, FP_SABER_OFFENSE, FP_SABER_DEFENSE,
    FP_SABERTHROW, NUM_FORCE_POWERS };
enum { FORCE_LEVEL_1 = 1, FORCE_LEVEL_3 = 3, NUM_FORCE_POWER_LEVELS = 4,
    NUM_FORCE_MASTERY_LEVELS = 8, MAX_FORCE_RANK = 7,
    FORCE_LIGHTSIDE = 1, FORCE_DARKSIDE = 2,
    GT_FFA = 0, GT_JEDIMASTER = 2, GT_DUEL = 3, GT_POWERDUEL = 4, GT_TEAM = 6,
    WP_NONE = 0, WP_SABER = 3, WP_NUM_WEAPONS = 19 };
#define DEFAULT_FORCEPOWERS "7-1-032330000000001333"
static struct { int integer; } ui_freeSaber;
static struct { int forceUiRulesInitialized, forceUiGametype, forceUiWeaponDisable; } cgs;
static int testGametype, testWeapons, testDuelWeapons, writes;
static char savedForcePowers[128];
static int engineFreeSaber, forceSaves;
static const char *Info_ValueForKey(const char *info, const char *key) {
    static char value[32];
    int number = !strcmp(key, "g_gametype") ? testGametype :
        !strcmp(key, "g_duelWeaponDisable") ? testDuelWeapons : testWeapons;
    snprintf(value, sizeof(value), "%d", number);
    return value;
}
static void SetCvar(const char *key, const char *value) {
    if (!strcmp(key, "forcepowers")) {
        CHECK(strlen(value) < sizeof(savedForcePowers));
        strcpy(savedForcePowers, value);
        ++forceSaves;
        return;
    }
    CHECK(!strcmp(key, "ui_freeSaber"));
    ui_freeSaber.integer = atoi(value);
    ++writes;
}
static void Noop(void) {}
static void WorldEffect(const char *command) { CHECK(!strcmp(command, "die")); }
static float CvarValue(const char *name) { return 0; }
enum { EXEC_APPEND };
static void ExecuteText(int mode, const char *text) {}
static struct {
    void (*Cvar_Set)(const char *, const char *);
    void (*FX_FreeSystem)(void);
    void (*ROFF_Clean)(void);
    void (*R_WorldEffectCommand)(const char *);
    float (*Cvar_VariableValue)(const char *);
    void (*Cmd_ExecuteText)(int, const char *);
} imports = { SetCvar, Noop, Noop, WorldEffect, CvarValue, ExecuteText }, *trap = &imports;
static struct { struct { int started, file; } log; } cg;
static void BG_ClearAnimsets(void) {}
static void CG_FreeCosmetics(void) {}
static void CG_DestroyAllGhoul2(void) {}
static void UI_CleanupGhoul2(void) {}
static void CG_LogPrintf(int file, const char *format, ...) {}
static void CG_CloseLog(int *file) {}
typedef int menuDef_t;
enum { FORCE_NONJEDI, FORCE_JEDI, TEAM_SPECTATOR = 3 };
static int uiForcePowersRank[NUM_FORCE_POWERS], uiForceUsed, uiForceAvailable;
static int uiMaxRank = 7, uiForceRank = 7, uiJediNonJedi;
static int uiForceSide, gTouchedForce;
static void UI_RegisterCvars(void) { ui_freeSaber.integer = engineFreeSaber; }
static char *UI_Cvar_VariableString(const char *name) {
    CHECK(!strcmp(name, "forcepowers"));
    return savedForcePowers;
}
static qboolean UI_TrueJediEnabled(void) { return qfalse; }
static const char *UI_TeamName(int team) { return "spectator"; }
void UI_UpdateClientForcePowers(const char *team);
static menuDef_t *Menus_FindByName(const char *name) { return NULL; }
static void Menu_ShowItemByName(menuDef_t *menu, const char *name, qboolean show) {}
static int Com_Clampi(int min, int max, int value) { return value < min ? min : value > max ? max : value; }
static void Q_strncpyz(char *out, const char *in, size_t size) { CHECK(strlen(in) < size); strcpy(out, in); }
static void Q_strcat(char *out, size_t size, const char *in) { CHECK(strlen(out) + strlen(in) < size); strcat(out, in); }
static char *va(const char *format, ...) {
    static char buffer[256]; va_list args;
    va_start(args, format); vsnprintf(buffer, sizeof(buffer), format, args); va_end(args); return buffer;
}
qboolean BG_LegalizedForcePowers2(char *, size_t, int, qboolean, int, int, int, qboolean);
#include "actual.h"
static int UiCost(const char *loadout) {
    int power, rank, used = 0;
    const char *ranks = strchr(strchr(loadout, '-') + 1, '-') + 1;
    for (power = 0; power < NUM_FORCE_POWERS; ++power)
        for (rank = 1; rank <= ranks[power] - '0'; ++rank)
            used += UI_ForcePowerCost(power, rank);
    return used;
}
int main(void) {
    int freeSaber, n, p, zeroPointCases = 0;
    unsigned seed = 184;
    const int saberOnly = ((1 << WP_NUM_WEAPONS) - 1) & ~(1 << WP_SABER);
    char loadout[128], original[128];
    /* Exact screenshot request. Engine cvar survives UI reload, but the
       newly loaded UI VM starts with a zero-initialized cvar mirror. */
    strcpy(savedForcePowers, "7-1-330103000333000330");
    engineFreeSaber = 1;
    ui_freeSaber.integer = 0;
    /* Old startup order reproduces the reported request change on menu open. */
    UI_UpdateForcePowers();
    UI_RegisterCvars();
    UpdateForceUsed();
    CHECK(uiForceAvailable == 8 && uiForcePowersRank[FP_SABER_DEFENSE] == 2);
    UI_UpdateClientForcePowers(NULL);
    CHECK(!strcmp(savedForcePowers, "7-1-330103000333000320"));
    strcpy(savedForcePowers, "7-1-330103000333000330");
    forceSaves = 0;
    ui_freeSaber.integer = 0;
    TestUiInitOrder();
    UpdateForceUsed(); /* First refresh after registration cannot undo trimming. */
    CHECK(uiForceAvailable == 0 && uiForcePowersRank[FP_SABER_DEFENSE] == 3);
    UI_UpdateClientForcePowers(NULL); /* Force menu onOpen: setForce none. */
    CHECK(!strcmp(savedForcePowers, "7-1-330103000333000330"));
    CHECK(forceSaves == 1);
    /* Genuine paid-saber rules must still enforce their additional cost. */
    engineFreeSaber = 0;
    ui_freeSaber.integer = 1;
    TestUiInitOrder();
    CHECK(uiForceAvailable == 6 && uiForcePowersRank[FP_SABER_DEFENSE] == 2);
    /* Reported full light-side build: jump, all light powers, offense/defense
       at rank 3, then the last point in Push. Reconnect must not trim defense. */
    memset(uiForcePowersRank, 0, sizeof(uiForcePowersRank));
    uiForcePowersRank[FP_HEAL] = uiForcePowersRank[FP_LEVITATION] = 3;
    uiForcePowersRank[FP_TELEPATHY] = uiForcePowersRank[FP_PROTECT] = 3;
    uiForcePowersRank[FP_ABSORB] = uiForcePowersRank[FP_TEAM_HEAL] = 3;
    uiForcePowersRank[FP_SABER_OFFENSE] = uiForcePowersRank[FP_SABER_DEFENSE] = 3;
    uiForcePowersRank[FP_PUSH] = 1;
    ui_freeSaber.integer = 1;
    UpdateForceUsed();
    CHECK(uiForceAvailable == 0 && uiForcePowersRank[FP_SABER_DEFENSE] == 3);
    /* Reproduce the old teardown behavior: two temporary extra costs drop
       the eight-point rank, which is not restored when the rule comes back. */
    ui_freeSaber.integer = 0;
    UpdateForceUsed();
    ui_freeSaber.integer = 1;
    UpdateForceUsed();
    CHECK(uiForceAvailable == 8 && uiForcePowersRank[FP_SABER_DEFENSE] == 2);
    uiForcePowersRank[FP_SABER_DEFENSE] = 3;
    UpdateForceUsed();
    CG_Shutdown();
    UpdateForceUsed(); /* UI frame between shutdown and the next gamestate. */
    CHECK(ui_freeSaber.integer == 1);
    CHECK(uiForceAvailable == 0 && uiForcePowersRank[FP_SABER_DEFENSE] == 3);
    testWeapons = saberOnly;
    memset(&cgs, 0, sizeof(cgs));
    CG_SyncFreeSaber("");
    UpdateForceUsed();
    CHECK(uiForceAvailable == 0 && uiForcePowersRank[FP_SABER_DEFENSE] == 3);
    /* A different server still replaces the preserved rule with paid costs. */
    CG_Shutdown();
    memset(&cgs, 0, sizeof(cgs));
    testWeapons = 0;
    CG_SyncFreeSaber("");
    CHECK(ui_freeSaber.integer == 0);
    UpdateForceUsed();
    CHECK(uiForceAvailable == 6 && uiForcePowersRank[FP_SABER_DEFENSE] == 2);
    memset(&cgs, 0, sizeof(cgs));
    writes = 0;
    /* First map must replace archived/previous-server state without an event. */
    ui_freeSaber.integer = 1;
    CG_SyncFreeSaber("");
    CHECK(ui_freeSaber.integer == 0 && writes == 1);
    testWeapons = saberOnly;
    CG_SyncFreeSaber("");
    CHECK(ui_freeSaber.integer == 1);
    /* Explicit JA+ event overrides survive unrelated serverinfo changes. */
    ui_freeSaber.integer = 0;
    n = writes;
    CG_SyncFreeSaber("");
    CHECK(ui_freeSaber.integer == 0 && writes == n);
    /* Map restart resets the cached inputs and reconstructs the rule. */
    cgs.forceUiRulesInitialized = 0;
    CG_SyncFreeSaber("");
    CHECK(ui_freeSaber.integer == 1);
    testGametype = GT_DUEL;
    CG_SyncFreeSaber("");
    CHECK(ui_freeSaber.integer == 0);
    testDuelWeapons = saberOnly;
    CG_SyncFreeSaber("");
    CHECK(ui_freeSaber.integer == 1);
    testGametype = GT_POWERDUEL;
    CG_SyncFreeSaber("");
    CHECK(ui_freeSaber.integer == 1);
    testGametype = GT_JEDIMASTER;
    CG_SyncFreeSaber("");
    CHECK(ui_freeSaber.integer == 0);
    testGametype = GT_FFA;
    testWeapons = saberOnly & ~(1 << (WP_NUM_WEAPONS - 1));
    CG_SyncFreeSaber("");
    CHECK(ui_freeSaber.integer == 0); /* Match server's non-pickup weapon bits. */
    for (freeSaber = 0; freeSaber <= 1; ++freeSaber) {
        ui_freeSaber.integer = freeSaber;
        CHECK(UI_ForcePowerCost(FP_SABER_OFFENSE, 1) == !freeSaber);
        CHECK(UI_ForcePowerCost(FP_SABER_DEFENSE, 1) == !freeSaber);
        CHECK(UI_ForcePowerCost(FP_SABER_OFFENSE, 2) == 5);
        CHECK(bgForcePowerCost[FP_SABER_OFFENSE][1] == 1);
        CHECK(bgForcePowerCost[FP_SABER_DEFENSE][1] == 1);
        for (n = 0; n < 10000; ++n) {
            strcpy(loadout, n & 1 ? "7-1-" : "7-2-");
            for (p = 0; p < NUM_FORCE_POWERS; ++p) {
                seed = seed * 1664525u + 1013904223u;
                loadout[4 + p] = '0' + ((seed >> 24) & 3);
            }
            loadout[4 + NUM_FORCE_POWERS] = 0;
            BG_LegalizedForcePowers(loadout, sizeof(loadout), 7, freeSaber, 0, GT_FFA, 0);
            CHECK(UiCost(loadout) <= 100);
            if (UiCost(loadout) == 100) ++zeroPointCases;
            strcpy(original, loadout);
            /* UI repaint and switching rules must never modify base costs. */
            ui_freeSaber.integer = !freeSaber;
            (void)UiCost(loadout);
            ui_freeSaber.integer = freeSaber;
            CHECK(BG_LegalizedForcePowers(loadout, sizeof(loadout), 7, freeSaber, 0, GT_FFA, 0));
            CHECK(!strcmp(original, loadout));
        }
    }
    CHECK(zeroPointCases > 0);
    printf("PASS: eight-point reconnect regression, shutdown/new-server rules, rule initialization/transitions/restart/overrides, duel rules, immutable costs, 20000 loadouts (%d fully spent), stable repeated validation.\n", zeroPointCases);
    return 0;
}
