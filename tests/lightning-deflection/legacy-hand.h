// Stock hand-animation branch from bg_pmove.c at afb63879a^.
// Raven/Activision/OpenJK code, GPL-2.0-or-later; see repository LICENSE.txt.
// Frozen compatibility reference: do not update it to match our new client.
static void LegacyHand(void) {
	if (0) {}
	else if (pm->ps->forceHandExtend != HANDEXTEND_NONE)
	{ //nothing else should be allowed to happen during this time, including weapon fire
		int desiredAnim = 0;
		qboolean seperateOnTorso = qfalse;
		qboolean playFullBody = qfalse;
		int desiredOnTorso = 0;

		switch(pm->ps->forceHandExtend)
		{
		case HANDEXTEND_FORCEPUSH:
			desiredAnim = BOTH_FORCEPUSH;
			break;
		case HANDEXTEND_FORCEPULL:
			desiredAnim = BOTH_FORCEPULL;
			break;
		case HANDEXTEND_FORCE_HOLD:
			if ( (pm->ps->fd.forcePowersActive&(1<<FP_GRIP)) )
			{//gripping
				desiredAnim = BOTH_FORCEGRIP_HOLD;
			}
			else if ( (pm->ps->fd.forcePowersActive&(1<<FP_LIGHTNING)) )
			{//lightning
				if ( pm->ps->weapon == WP_MELEE
					&& pm->ps->activeForcePass > FORCE_LEVEL_2 )
				{//2-handed lightning
					desiredAnim = BOTH_FORCE_2HANDEDLIGHTNING_HOLD;
				}
				else
				{
					desiredAnim = BOTH_FORCELIGHTNING_HOLD;
				}
			}
			else if ( (pm->ps->fd.forcePowersActive&(1<<FP_DRAIN)) )
			{//draining
				desiredAnim = BOTH_FORCEGRIP_HOLD;
			}
			else
			{//???
				desiredAnim = BOTH_FORCEGRIP_HOLD;
			}
			break;
		case HANDEXTEND_SABERPULL:
			desiredAnim = BOTH_SABERPULL;
			break;
		case HANDEXTEND_CHOKE:
			desiredAnim = BOTH_CHOKE3; //left-handed choke
			break;
		case HANDEXTEND_DODGE:
			desiredAnim = pm->ps->forceDodgeAnim;
			break;
		case HANDEXTEND_KNOCKDOWN:
			if (pm->ps->forceDodgeAnim)
			{
				if (pm->ps->forceDodgeAnim > 4)
				{ //this means that we want to play a sepereate anim on the torso
					int originalDAnim = pm->ps->forceDodgeAnim-8; //-8 is the original legs anim
					if (originalDAnim == 2)
					{
						desiredAnim = BOTH_FORCE_GETUP_B1;
					}
					else if (originalDAnim == 3)
					{
						desiredAnim = BOTH_FORCE_GETUP_B3;
					}
					else
					{
						desiredAnim = BOTH_GETUP1;
					}

					//now specify the torso anim
					seperateOnTorso = qtrue;
					desiredOnTorso = BOTH_FORCEPUSH;
				}
				else if (pm->ps->forceDodgeAnim == 2)
				{
					desiredAnim = BOTH_FORCE_GETUP_B1;
				}
				else if (pm->ps->forceDodgeAnim == 3)
				{
					desiredAnim = BOTH_FORCE_GETUP_B3;
				}
				else
				{
					desiredAnim = BOTH_GETUP1;
				}
			}
			else
			{
				desiredAnim = BOTH_KNOCKDOWN1;
			}
			break;
		case HANDEXTEND_DUELCHALLENGE:
			desiredAnim = BOTH_ENGAGETAUNT;
			break;
		case HANDEXTEND_TAUNT:
			desiredAnim = pm->ps->forceDodgeAnim;
			if ( desiredAnim != BOTH_ENGAGETAUNT
				&& VectorCompare( pm->ps->velocity, vec3_origin )
				&& pm->ps->groundEntityNum != ENTITYNUM_NONE )
			{
				playFullBody = qtrue;
			}
			break;
		case HANDEXTEND_PRETHROW:
			desiredAnim = BOTH_A3_TL_BR;
			playFullBody = qtrue;
			break;
		case HANDEXTEND_POSTTHROW:
			desiredAnim = BOTH_D3_TL___;
			playFullBody = qtrue;
			break;
		case HANDEXTEND_PRETHROWN:
			desiredAnim = BOTH_KNEES1;
			playFullBody = qtrue;
			break;
		case HANDEXTEND_POSTTHROWN:
			if (pm->ps->forceDodgeAnim)
			{
				desiredAnim = BOTH_FORCE_GETUP_F2;
			}
			else
			{
				desiredAnim = BOTH_KNOCKDOWN5;
			}
			playFullBody = qtrue;
			break;
		case HANDEXTEND_DRAGGING:
			desiredAnim = BOTH_B1_BL___;
			break;
		case HANDEXTEND_JEDITAUNT:
			desiredAnim = BOTH_GESTURE1;
			//playFullBody = qtrue;
			break;
			//Hmm... maybe use these, too?
			//BOTH_FORCEHEAL_QUICK //quick heal (SP level 2 & 3)
			//BOTH_MINDTRICK1 // wave (maybe for mind trick 2 & 3 - whole area, and for force seeing)
			//BOTH_MINDTRICK2 // tap (maybe for mind trick 1 - one person)
			//BOTH_FORCEGRIP_START //start grip
			//BOTH_FORCEGRIP_HOLD //hold grip
			//BOTH_FORCEGRIP_RELEASE //release grip
			//BOTH_FORCELIGHTNING //quick lightning burst (level 1)
			//BOTH_FORCELIGHTNING_START //start lightning
			//BOTH_FORCELIGHTNING_HOLD //hold lightning
			//BOTH_FORCELIGHTNING_RELEASE //release lightning
		default:
			desiredAnim = BOTH_FORCEPUSH;
			break;
		}

		if (!seperateOnTorso)
		{ //of seperateOnTorso, handle it after setting the legs
			PM_SetAnim(SETANIM_TORSO, desiredAnim, SETANIM_FLAG_OVERRIDE|SETANIM_FLAG_HOLD);
			pm->ps->torsoTimer = 1;
		}

		if (playFullBody)
		{ //sorry if all these exceptions are getting confusing. This one just means play on both legs and torso.
			PM_SetAnim(SETANIM_BOTH, desiredAnim, SETANIM_FLAG_OVERRIDE|SETANIM_FLAG_HOLD);
			pm->ps->legsTimer = pm->ps->torsoTimer = 1;
		}
		else if (pm->ps->forceHandExtend == HANDEXTEND_DODGE || pm->ps->forceHandExtend == HANDEXTEND_KNOCKDOWN ||
			(pm->ps->forceHandExtend == HANDEXTEND_CHOKE && pm->ps->groundEntityNum == ENTITYNUM_NONE) )
		{ //special case, play dodge anim on whole body, choke anim too if off ground
			if (seperateOnTorso)
			{
				PM_SetAnim(SETANIM_LEGS, desiredAnim, SETANIM_FLAG_OVERRIDE|SETANIM_FLAG_HOLD);
				pm->ps->legsTimer = 1;

				PM_SetAnim(SETANIM_TORSO, desiredOnTorso, SETANIM_FLAG_OVERRIDE|SETANIM_FLAG_HOLD);
				pm->ps->torsoTimer = 1;
			}
			else
			{
				PM_SetAnim(SETANIM_LEGS, desiredAnim, SETANIM_FLAG_OVERRIDE|SETANIM_FLAG_HOLD);
				pm->ps->legsTimer = 1;
			}
		}

		return;
	}

}
