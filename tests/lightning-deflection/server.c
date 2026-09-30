#include "game/g_local.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); } } while (0)
gentity_t g_entities[MAX_ENTITIESTOTAL];
level_locals_t level;
vmCvar_t g_fixLightning;
static gclient_t clients[3];
static gentity_t eventEntity;
static pmove_t movement;
static pmove_t *pm = &movement;
static int damages, events, absorbCalls, absorbLevel = -1, forceAllowed = 1;

int ForcePowerUsableOn(gentity_t *attacker, gentity_t *target, forcePowers_t power) { return forceAllowed; }
int WP_AbsorbConversion(gentity_t *target, int absorb, gentity_t *attacker, int power, int rank, int spent) {
  ++absorbCalls; return absorbLevel;
}
void G_Damage(gentity_t *target, gentity_t *inflictor, gentity_t *attacker, vec3_t dir, vec3_t point, int damage, int flags, int mod) {
  CHECK(mod == MOD_FORCE_DARK); ++damages; target->health -= damage;
}
void G_SetAnim(gentity_t *ent, usercmd_t *cmd, int parts, int anim, int flags, int blend) {
  CHECK(parts == SETANIM_TORSO); CHECK(flags & SETANIM_FLAG_HOLD);
  ent->client->ps.torsoAnim = anim; ent->client->ps.torsoTimer = 100;
}
gentity_t *G_TempEntity(vec3_t origin, int event) {
  CHECK(event == EV_SABER_BLOCK); ++events; memset(&eventEntity, 0, sizeof(eventEntity)); eventEntity.s.event = event; return &eventEntity;
}
int G_SoundIndex(const char *name) { return 1; }
void G_Sound(gentity_t *ent, int channel, int sound) {}
void Jedi_Decloak(gentity_t *ent) { ent->client->ps.powerups[PW_CLOAKED] = 0; }
char *QDECL va(const char *format, ...) {
  static char buffer[256]; va_list args; va_start(args, format); vsnprintf(buffer, sizeof(buffer), format, args); va_end(args); return buffer;
}
#include "shared.h"
#include "server.h"

static void Reset(void) {
  int i;
  memset(clients, 0, sizeof(clients)); memset(g_entities, 0, sizeof(g_entities));
  memset(&movement, 0, sizeof(movement)); memset(&level, 0, sizeof(level));
  level.time = 1000; g_fixLightning.integer = 3; damages = events = absorbCalls = 0;
  absorbLevel = -1; forceAllowed = 1;
  for (i = 0; i < 3; ++i) {
    playerState_t *ps = &clients[i].ps;
    g_entities[i].client = &clients[i]; g_entities[i].inuse = qtrue;
    g_entities[i].takedamage = qtrue; g_entities[i].health = 100; g_entities[i].s.number = i;
    ps->clientNum = i; ps->pm_type = PM_NORMAL; ps->stats[STAT_HEALTH] = 100;
    ps->weapon = WP_SABER; ps->saberEntityNum = 10; ps->saberMove = LS_READY;
    ps->groundEntityNum = ENTITYNUM_WORLD; ps->speed = ps->basespeed = 250; ps->viewheight = DEFAULT_VIEWHEIGHT;
    ps->fd.forcePowerLevel[FP_SABER_DEFENSE] = FORCE_LEVEL_3;
    ps->fd.forcePowerMax = 100;
    clients[i].pers.cmd.weapon = WP_SABER; clients[i].pers.cmd.serverTime = level.time;
  }
  clients[0].ps.origin[0] = 100;
  clients[0].ps.viewangles[YAW] = 180;
  clients[0].ps.fd.forcePowerLevel[FP_LIGHTNING] = FORCE_LEVEL_3;
  clients[0].ps.fd.forcePowersActive = 1 << FP_LIGHTNING;
  clients[2].ps.origin[0] = 100; clients[2].ps.origin[1] = 20;
  clients[2].ps.viewangles[YAW] = 180;
  clients[2].ps.fd.forcePowersActive = 1 << FP_LIGHTNING;
  movement.ps = &clients[1].ps; movement.cmd = clients[1].pers.cmd;
}
static void Hit(int caster) {
  vec3_t direction = {-1, 0, 0}, impact = {0, 0, DEFAULT_VIEWHEIGHT};
  ForceLightningDamage(&g_entities[caster], &g_entities[1], direction, impact);
}
static void Requirements(void) {
  playerState_t *ps = &clients[1].ps; usercmd_t *cmd = &clients[1].pers.cmd;
  CHECK(BG_CanDeflectLightning(ps, cmd, level.time)); // No learned Lightning or Force points.
  ps->fd.forcePowerLevel[FP_SABER_DEFENSE] = FORCE_LEVEL_2;
  CHECK(!BG_CanDeflectLightning(ps, cmd, level.time)); ps->fd.forcePowerLevel[FP_SABER_DEFENSE] = FORCE_LEVEL_3;
  cmd->forwardmove = 64; cmd->buttons = BUTTON_WALKING; ps->velocity[0] = 125;
  CHECK(BG_CanDeflectLightning(ps, cmd, level.time));
  cmd->rightmove = -64; CHECK(BG_CanDeflectLightning(ps, cmd, level.time));
  cmd->buttons = 0; CHECK(!BG_CanDeflectLightning(ps, cmd, level.time));
  cmd->buttons = BUTTON_WALKING; ps->velocity[0] = 250;
  CHECK(!BG_CanDeflectLightning(ps, cmd, level.time));
  ps->velocity[0] = 0; cmd->forwardmove = 127;
  CHECK(!BG_CanDeflectLightning(ps, cmd, level.time)); cmd->forwardmove = 64;
  ps->velocity[0] = 0; ps->saberHolstered = 2; CHECK(!BG_CanDeflectLightning(ps, cmd, level.time));
  ps->saberHolstered = 0; ps->saberInFlight = qtrue; CHECK(!BG_CanDeflectLightning(ps, cmd, level.time));
  ps->saberInFlight = qfalse; ps->groundEntityNum = ENTITYNUM_NONE; CHECK(!BG_CanDeflectLightning(ps, cmd, level.time));
  ps->groundEntityNum = ENTITYNUM_WORLD; ps->saberMove = LS_A_T2B; CHECK(!BG_CanDeflectLightning(ps, cmd, level.time));
  ps->saberMove = LS_READY; ps->m_iVehicleNum = 1; CHECK(!BG_CanDeflectLightning(ps, cmd, level.time));
  ps->m_iVehicleNum = 0; ps->forceHandExtend = HANDEXTEND_KNOCKDOWN; CHECK(!BG_CanDeflectLightning(ps, cmd, level.time));
  ps->groundEntityNum = ENTITYNUM_WORLD; ps->saberMove = LS_A_T2B; CHECK(!BG_CanDeflectLightning(ps, cmd, level.time));
  ps->saberMove = LS_READY; ps->m_iVehicleNum = 1; CHECK(!BG_CanDeflectLightning(ps, cmd, level.time));
  ps->m_iVehicleNum = 0; ps->forceHandExtend = HANDEXTEND_KNOCKDOWN; CHECK(!BG_CanDeflectLightning(ps, cmd, level.time));
  ps->forceHandExtend = HANDEXTEND_NONE; ps->weaponTime = 100; ps->saberBlocked = BLOCKED_UPPER_RIGHT;
  cmd->weapon = WP_MELEE; CHECK(!BG_CanDeflectLightning(ps, cmd, level.time)); // Wait for combat recovery to finish.
  ps->weaponTime = 0; CHECK(BG_CanDeflectLightning(ps, cmd, level.time)); // Stale input/block state alone is harmless.
}
static void Aiming(void) {
  playerState_t *ps = &clients[1].ps; vec3_t source = {100, 0, DEFAULT_VIEWHEIGHT}; int anim;
  CHECK(BG_LightningDeflectDirection(ps, source, &anim)); CHECK(anim == BOTH_P1_S1_TR);
  source[1] = 50; CHECK(BG_LightningDeflectDirection(ps, source, &anim)); CHECK(anim == BOTH_P1_S1_TL);
  source[1] = -50; CHECK(BG_LightningDeflectDirection(ps, source, &anim)); CHECK(anim == BOTH_P1_S1_TR);
  source[1] = 0; ps->viewangles[YAW] = 49; CHECK(BG_LightningDeflectDirection(ps, source, NULL));
  ps->viewangles[YAW] = 51; CHECK(!BG_LightningDeflectDirection(ps, source, NULL));
  ps->viewangles[YAW] = 90; CHECK(!BG_LightningDeflectDirection(ps, source, NULL));
  ps->viewangles[YAW] = 180; CHECK(!BG_LightningDeflectDirection(ps, source, NULL));
  ps->viewangles[YAW] = 0; ps->viewangles[PITCH] = 60; CHECK(!BG_LightningDeflectDirection(ps, source, NULL));
  ps->viewangles[PITCH] = -45; source[2] += 100; CHECK(BG_LightningDeflectDirection(ps, source, NULL));
}
static void Interruption(void) {
  int i; const int buttons[] = {BUTTON_ATTACK, BUTTON_ALT_ATTACK, BUTTON_USE_HOLDABLE, BUTTON_GESTURE};
  for (i = 0; i < 4; ++i) {
    Reset(); Hit(0); movement.cmd.buttons = buttons[i]; PM_UpdateLightningDeflect();
    CHECK(clients[1].ps.forceHandExtend == HANDEXTEND_NONE);
    CHECK(!(clients[1].ps.eFlags2 & EF2_LIGHTNING_DEFLECT));
    CHECK(clients[1].ps.torsoTimer == 0 && clients[1].ps.weaponTime == 0);
    CHECK(movement.cmd.buttons == buttons[i]);
  }
  Reset(); Hit(0); movement.cmd.forwardmove = 127; PM_UpdateLightningDeflect();
  CHECK(clients[1].ps.forceHandExtend == HANDEXTEND_NONE);
  Reset(); Hit(0); movement.cmd.upmove = 127; PM_UpdateLightningDeflect();
  CHECK(clients[1].ps.forceHandExtend == HANDEXTEND_NONE);
}
static void Combat(void) {
  int i;
  // No combat move may acquire/reacquire the guard, even with attack released.
  for (i = LS_NONE; i < LS_MOVE_MAX; ++i) {
    if (i == LS_NONE || i == LS_READY) continue;
    Reset(); clients[1].ps.saberMove = i;
    clients[1].ps.torsoAnim = BOTH_A1_T__B_; clients[1].ps.torsoTimer = 250;
    clients[1].ps.weaponTime = 0;
    Hit(0);
    CHECK(damages == 1 && clients[1].ps.forceHandExtend == HANDEXTEND_NONE);
    CHECK(clients[1].ps.torsoAnim == BOTH_A1_T__B_ && clients[1].ps.torsoTimer == 250);
    CHECK(clients[1].ps.weaponTime == 0 && clients[1].ps.saberMove == i);
    Reset(); Hit(0);
    clients[1].ps.saberMove = i; clients[1].ps.torsoAnim = BOTH_A1_T__B_;
    clients[1].ps.torsoTimer = 250; clients[1].ps.weaponTime = 250;
    PM_UpdateLightningDeflect();
    CHECK(clients[1].ps.forceHandExtend == HANDEXTEND_NONE);
    CHECK(clients[1].ps.torsoTimer == 250 && clients[1].ps.weaponTime == 250);
  }
  Reset(); clients[1].ps.weaponTime = 100; Hit(0);
  CHECK(damages == 1 && clients[1].ps.forceHandExtend == HANDEXTEND_NONE);
  Reset(); clients[1].ps.saberLockTime = level.time + 100; Hit(0);
  CHECK(damages == 1 && clients[1].ps.forceHandExtend == HANDEXTEND_NONE);
}
static void Damage(void) {
  Hit(0); CHECK(damages == 0 && absorbCalls == 0); CHECK(g_entities[1].health == 100);
  CHECK(clients[1].ps.electrifyTime == 0 && clients[1].ps.fd.forcePower == 0);
  CHECK(clients[1].ps.torsoAnim == BOTH_P1_S1_TR); CHECK(clients[1].ps.legsAnim == 0);
  CHECK(eventEntity.s.eventParm == LIGHTNING_DEFLECT_EVENT_PARM);
  CHECK(eventEntity.s.eFlags2 & EF2_LIGHTNING_DEFLECT);
  CHECK(eventEntity.s.otherEntityNum == 0 && eventEntity.s.otherEntityNum2 == 1);
  clients[1].pers.cmd.buttons = BUTTON_ATTACK; Hit(0);
  CHECK(damages == 1 && g_entities[1].health == 99); CHECK(clients[1].ps.electrifyTime > level.time);
  CHECK(clients[1].ps.forceHandExtend == HANDEXTEND_NONE && !(clients[1].ps.eFlags2 & EF2_LIGHTNING_DEFLECT));
  clients[1].pers.cmd.buttons = 0; Hit(0); CHECK(damages == 2); // Cannot enter guard during an existing shock.
  Reset(); clients[1].ps.viewangles[YAW] = 90; Hit(0); CHECK(damages == 1);
  Reset(); clients[1].pers.cmd.forwardmove = 127; Hit(0); CHECK(damages == 1);
  CHECK(g_entities[0].health == 100); // Scattered lightning never reflects damage.
}
  Reset(); clients[1].ps.viewangles[YAW] = 90; Hit(0); CHECK(damages == 1);
  Reset(); clients[1].pers.cmd.forwardmove = 127; Hit(0); CHECK(damages == 1);
  CHECK(g_entities[0].health == 100); // Scattered lightning never reflects damage.
}
static void Lifecycle(void) {
  Hit(0); CHECK(events == 1); level.time += 50; Hit(0); CHECK(events == 1);
  level.time += 50; Hit(0); CHECK(events == 2);
  level.time += LIGHTNING_DEFLECT_HOLD_TIME; WP_UpdateLightningDeflect(&g_entities[1], &clients[1].pers.cmd);
  CHECK(clients[1].ps.forceHandExtend == HANDEXTEND_NONE && clients[1].ps.weaponTime == 0);
  Reset(); Hit(0); clients[0].ps.fd.forcePowersActive = 0;
  WP_UpdateLightningDeflect(&g_entities[1], &clients[1].pers.cmd); CHECK(!(clients[1].ps.eFlags2 & EF2_LIGHTNING_DEFLECT));
  Reset(); Hit(0); clients[1].ps.viewangles[YAW] = 90;
  WP_UpdateLightningDeflect(&g_entities[1], &clients[1].pers.cmd); CHECK(clients[1].ps.forceHandExtend == HANDEXTEND_NONE);
  Reset(); Hit(0); clients[1].ps.forceHandExtend = HANDEXTEND_KNOCKDOWN; clients[1].ps.forceDodgeAnim = 2;
  WP_UpdateLightningDeflect(&g_entities[1], &clients[1].pers.cmd);
  CHECK(clients[1].ps.forceHandExtend == HANDEXTEND_KNOCKDOWN && clients[1].ps.forceDodgeAnim == 2);
}
static void Sources(void) {
  Hit(0); Hit(2); CHECK(damages == 0 && events == 2);
  CHECK(clients[1].ps.torsoAnim == BOTH_P1_S1_TL);
  level.time += 50; Hit(0); Hit(2); CHECK(events == 2 && damages == 0);
  Reset(); clients[1].ps.origin[1] = 20; Hit(0);
  CHECK(clients[1].ps.torsoAnim == BOTH_P1_S1_TR);
  Reset(); clients[1].ps.origin[1] = -20; Hit(0);
  CHECK(clients[1].ps.torsoAnim == BOTH_P1_S1_TL);
  Reset(); Hit(0);
  clients[2].ps.origin[0] = 0; clients[2].ps.origin[1] = 100; Hit(2);
  CHECK(damages == 1 && !(clients[1].ps.eFlags2 & EF2_LIGHTNING_DEFLECT));
}
static void Absorption(void) {
  forceAllowed = 0; Hit(0); CHECK(damages == 0 && events == 0);
  forceAllowed = 1; clients[1].ps.fd.forcePowerLevel[FP_SABER_DEFENSE] = FORCE_LEVEL_2;
  absorbLevel = 0; Hit(0); CHECK(damages == 0 && absorbCalls == 1);
  level.time += 50; Hit(0); CHECK(clients[1].ps.fd.forcePower == 1 && absorbCalls == 1);
}
int main(int argc, char **argv) {
  CHECK(argc == 2); Reset();
  if (!strcmp(argv[1], "requirements")) Requirements();
  else if (!strcmp(argv[1], "aiming")) Aiming();
  else if (!strcmp(argv[1], "interruption")) Interruption();
  else if (!strcmp(argv[1], "combat")) Combat();
  else if (!strcmp(argv[1], "damage")) Damage();
  else if (!strcmp(argv[1], "lifecycle")) Lifecycle();
  else if (!strcmp(argv[1], "sources")) Sources();
  else if (!strcmp(argv[1], "absorption")) Absorption();
  else CHECK(0);
  puts("Passed"); return 0;
}
