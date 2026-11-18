/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/
// Frozen pre-index production selector; no Git history needed by default.
void CG_PlayerAnimEvents( int animFileIndex, int eventFileIndex, qboolean torso, int oldFrame, int frame, int entNum )
{
	int		i;
	int		firstFrame = 0, lastFrame = 0;
	qboolean	doEvent = qfalse, inSameAnim = qfalse, loopAnim = qfalse, match = qfalse, animBackward = qfalse;
	qboolean grappleFrames = qfalse;
	animevent_t *animEvents = NULL;

	if ( torso )
	{
		animEvents = bgAllEvents[eventFileIndex].torsoAnimEvents;
	}
	else
	{
		animEvents = bgAllEvents[eventFileIndex].legsAnimEvents;
	}
	if ( fabs((float)(oldFrame-frame)) > 1 )
	{//given a range, see if keyFrame falls in that range
		int oldAnim, anim;
		if ( torso )
		{
			/*
			if ( cg_reliableAnimSounds.integer > 1 )
			{//more precise, slower
				oldAnim = PM_TorsoAnimForFrame( &g_entities[entNum], oldFrame );
				anim = PM_TorsoAnimForFrame( &g_entities[entNum], frame );
			}
			else
			*/
			{//less precise, but faster
				oldAnim = cg_entities[entNum].currentState.torsoAnim;
				anim = cg_entities[entNum].nextState.torsoAnim;
			}
		}
		else
		{
			/*
			if ( cg_reliableAnimSounds.integer > 1 )
			{//more precise, slower
				oldAnim = PM_LegsAnimForFrame( &g_entities[entNum], oldFrame );
				anim = PM_TorsoAnimForFrame( &g_entities[entNum], frame );
			}
			else
			*/
			{//less precise, but faster
				oldAnim = cg_entities[entNum].currentState.legsAnim;
				anim = cg_entities[entNum].nextState.legsAnim;
			}
		}
		// nextState can already contain the release/get-up while Ghoul2 is
		// still playing the current kata. Its sampled frames, not the future
		// snapshot's animation, decide whether an impact was crossed.
		if ( BG_IsGrappleSoundAnim( oldAnim ) )
		{
			const animation_t *playing = &bgAllAnims[animFileIndex].anims[oldAnim];
			if ( oldFrame >= playing->firstFrame &&
				oldFrame < playing->firstFrame + playing->numFrames &&
				frame >= playing->firstFrame &&
				frame < playing->firstFrame + playing->numFrames )
			{
				anim = oldAnim;
			}
		}
		if ( anim != oldAnim )
		{//not in same anim
			inSameAnim = qfalse;
			//FIXME: we *could* see if the oldFrame was *just about* to play the keyframed sound...
		}
		else
		{//still in same anim, check for looping anim
			animation_t *animation;

			inSameAnim = qtrue;
			animation = &bgAllAnims[animFileIndex].anims[anim];
			// Paired melee sounds must survive skipped render frames. Only relax
			// the proximity check when both sampled frames belong to this move;
			// a stale frame from another animation must not trigger its events.
			grappleFrames = BG_IsGrappleSoundAnim( anim ) &&
				oldFrame >= animation->firstFrame &&
				oldFrame < animation->firstFrame + animation->numFrames &&
				frame >= animation->firstFrame &&
				frame < animation->firstFrame + animation->numFrames;
			animBackward = (animation->frameLerp<0);
			if ( animation->loopFrames != -1 )
			{//a looping anim!
				loopAnim = qtrue;
				firstFrame = animation->firstFrame;
				lastFrame = animation->firstFrame+animation->numFrames;
			}
		}
	}

	// Check for anim sound
	for (i=0;i<MAX_ANIM_EVENTS;++i)
	{
		if ( animEvents[i].eventType == AEV_NONE )	// No event, end of list
		{
			break;
		}

		match = qfalse;
		if ( animEvents[i].keyFrame == frame )
		{//exact match
			match = qtrue;
		}
		else if ( fabs((float)(oldFrame-frame)) > 1 /*&& cg_reliableAnimSounds.integer*/ )
		{//given a range, see if keyFrame falls in that range
			if ( inSameAnim )
			{//if changed anims altogether, sorry, the sound is lost
				if ( grappleFrames || fabs((float)(oldFrame-animEvents[i].keyFrame)) <= 3
					 || fabs((float)(frame-animEvents[i].keyFrame)) <= 3 )
				{//must be at least close to the keyframe
					if ( animBackward )
					{//animation plays backwards
						if ( oldFrame > animEvents[i].keyFrame && frame < animEvents[i].keyFrame )
						{//old to new passed through keyframe
							match = qtrue;
						}
						else if ( loopAnim )
						{//hmm, didn't pass through it linearally, see if we looped
							if ( animEvents[i].keyFrame >= firstFrame && animEvents[i].keyFrame < lastFrame )
							{//keyframe is in this anim
								if ( oldFrame > animEvents[i].keyFrame
									&& frame > oldFrame )
								{//old to new passed through keyframe
									match = qtrue;
								}
							}
						}
					}
					else
					{//anim plays forwards
						if ( oldFrame < animEvents[i].keyFrame && frame > animEvents[i].keyFrame )
						{//old to new passed through keyframe
							match = qtrue;
						}
						else if ( loopAnim )
						{//hmm, didn't pass through it linearally, see if we looped
							if ( animEvents[i].keyFrame >= firstFrame && animEvents[i].keyFrame < lastFrame )
							{//keyframe is in this anim
								if ( oldFrame < animEvents[i].keyFrame
									&& frame < oldFrame )
								{//old to new passed through keyframe
									match = qtrue;
								}
							}
						}
					}
				}
			}
		}
		if ( match )
		{
			switch ( animEvents[i].eventType )
			{
			case AEV_SOUND:
			case AEV_SOUNDCHAN:
				// Determine probability of playing sound
				if (!animEvents[i].eventData[AED_SOUND_PROBABILITY])	// 100%
				{
					doEvent = qtrue;
				}
				else if (animEvents[i].eventData[AED_SOUND_PROBABILITY] > Q_irand(0, 99) )
				{
					doEvent = qtrue;
				}
				break;
			case AEV_SABER_SWING:
				// Determine probability of playing sound
				if (!animEvents[i].eventData[AED_SABER_SWING_PROBABILITY])	// 100%
				{
					doEvent = qtrue;
				}
				else if (animEvents[i].eventData[AED_SABER_SWING_PROBABILITY] > Q_irand(0, 99) )
				{
					doEvent = qtrue;
				}
				break;
			case AEV_SABER_SPIN:
				// Determine probability of playing sound
				if (!animEvents[i].eventData[AED_SABER_SPIN_PROBABILITY])	// 100%
				{
					doEvent = qtrue;
				}
				else if (animEvents[i].eventData[AED_SABER_SPIN_PROBABILITY] > Q_irand(0, 99) )
				{
					doEvent = qtrue;
				}
				break;
			case AEV_FOOTSTEP:
				// Determine probability of playing sound
				if (!animEvents[i].eventData[AED_FOOTSTEP_PROBABILITY])	// 100%
				{
					doEvent = qtrue;
				}
				else if (animEvents[i].eventData[AED_FOOTSTEP_PROBABILITY] > Q_irand(0, 99) )
				{
					doEvent = qtrue;
				}
				break;
			case AEV_EFFECT:
				// Determine probability of playing sound
				if (!animEvents[i].eventData[AED_EFFECT_PROBABILITY])	// 100%
				{
					doEvent = qtrue;
				}
				else if (animEvents[i].eventData[AED_EFFECT_PROBABILITY] > Q_irand(0, 99) )
				{
					doEvent = qtrue;
				}
				break;
			case AEV_FIRE:
				// Determine probability of playing sound
				if (!animEvents[i].eventData[AED_FIRE_PROBABILITY])	// 100%
				{
					doEvent = qtrue;
				}
				else if (animEvents[i].eventData[AED_FIRE_PROBABILITY] > Q_irand(0, 99) )
				{
					doEvent = qtrue;
				}
				break;
			case AEV_MOVE:
				doEvent = qtrue;
				break;
			default:
				//doEvent = qfalse;//implicit
				break;
			}
			// do event
			if ( doEvent )
			{
				CG_PlayerAnimEventDo( &cg_entities[entNum], &animEvents[i] );
			}
		}
	}
}
