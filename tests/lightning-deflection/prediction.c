#include "game/bg_public.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
#include "shared.h"

int main(void) {
  playerState_t ps;
  usercmd_t cmd;
  memset(&ps, 0, sizeof(ps));
  memset(&cmd, 0, sizeof(cmd));
  ps.stats[STAT_HEALTH] = 100;
  ps.pm_type = PM_NORMAL;
  ps.weapon = WP_SABER;
  cmd.weapon = WP_SABER;
  ps.saberEntityNum = 10;
  ps.saberMove = LS_READY;
  ps.groundEntityNum = ENTITYNUM_WORLD;
  ps.basespeed = ps.speed = 250;
  ps.forceHandExtend = HANDEXTEND_LIGHTNING_DEFLECT;
  /* Defense stays zero in decoded snapshots: only the server knows its level. */
  CHECK(ps.fd.forcePowerLevel[FP_SABER_DEFENSE] == 0);
  CHECK(BG_CanDeflectLightning(&ps, &cmd, 1000));
  {
    int move;
    for (move = LS_NONE; move < LS_MOVE_MAX; ++move) {
      if (move == LS_NONE || move == LS_READY) continue;
      ps.saberMove = move;
      CHECK(!BG_CanDeflectLightning(&ps, &cmd, 1000));
    }
    ps.saberMove = LS_READY;
    ps.weaponTime = 100;
    CHECK(!BG_CanDeflectLightning(&ps, &cmd, 1000));
    ps.weaponTime = 0;
  }
  cmd.buttons = BUTTON_ATTACK;
  CHECK(!BG_CanDeflectLightning(&ps, &cmd, 1000));
  cmd.buttons = 0;
  cmd.forwardmove = 127;
  CHECK(!BG_CanDeflectLightning(&ps, &cmd, 1000));
  cmd.forwardmove = 0;
  cmd.upmove = 127;
  CHECK(!BG_CanDeflectLightning(&ps, &cmd, 1000));
  return 0;
}
