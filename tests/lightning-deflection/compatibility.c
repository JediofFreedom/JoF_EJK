#include "game/bg_public.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
#include "shared.h"
static playerState_t state;
static pmove_t movement;
static pmove_t *pm = &movement;
static void PM_SetAnim(int part, int anim, int flags) {
  (void)flags;
  if (part & SETANIM_TORSO) state.torsoAnim = anim;
  if (part & SETANIM_LEGS) state.legsAnim = anim;
}
#include "legacy-hand.h"
#include "modern-hand.h"

int main(void) {
  int side, walking;
  const int poses[] = {BOTH_P1_S1_TL, BOTH_P1_S1_TR};
  movement.ps = &state;
  // Reproduce the old prediction fallback for the previously sent private ID.
  state.forceHandExtend = HANDEXTEND_LIGHTNING_DEFLECT;
  state.forceDodgeAnim = BOTH_P1_S1_TL;
  LegacyHand();
  CHECK(state.torsoAnim == BOTH_FORCEPUSH);
  for (side = 0; side < 2; ++side) {
    for (walking = 0; walking < 2; ++walking) {
      memset(&state, 0, sizeof(state));
      state.weapon = WP_SABER;
      state.groundEntityNum = ENTITYNUM_WORLD;
      state.forceHandExtend = HANDEXTEND_TAUNT;
      state.forceDodgeAnim = poses[side];
      state.legsAnim = BOTH_WALK1;
      state.velocity[0] = walking ? 30 : 0;
      LegacyHand();
      CHECK(state.torsoAnim == poses[side]);
      // Stock taunt prediction uses both body parts while standing still.
      CHECK(state.legsAnim == (walking ? BOTH_WALK1 : poses[side]));
      state.legsAnim = BOTH_WALK1;
      ModernHand();
      CHECK(state.torsoAnim == poses[side] && state.legsAnim == BOTH_WALK1);
    }
  }
  // Regular taunts still use stock full-body behavior in both clients.
  state.forceDodgeAnim = BOTH_GESTURE1;
  VectorClear(state.velocity);
  CHECK(!BG_IsLightningDeflect(&state));
  LegacyHand();
  CHECK(state.torsoAnim == BOTH_GESTURE1 && state.legsAnim == BOTH_GESTURE1);
  state.legsAnim = BOTH_WALK1;
  ModernHand();
  CHECK(state.torsoAnim == BOTH_GESTURE1 && state.legsAnim == BOTH_GESTURE1);
  puts("Stock prediction displays both guards instead of Force Push; modern prediction preserves leg movement and ordinary taunts.");
  return 0;
}
