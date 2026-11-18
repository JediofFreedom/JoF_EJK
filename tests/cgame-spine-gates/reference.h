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
// Frozen production function before cheap-disqualifier reordering.
static void BG_G2ClientSpineAngles( void *ghoul2, int motionBolt, vec3_t cent_lerpOrigin, vec3_t cent_lerpAngles, entityState_t *cent,
							int time, vec3_t viewAngles, int ciLegs, int ciTorso, const vec3_t angles, vec3_t thoracicAngles,
							vec3_t ulAngles, vec3_t llAngles, vec3_t modelScale, float *tPitchAngle, float *tYawAngle, int *corrTime )
{
	qboolean doCorr = qfalse;

	//*tPitchAngle = viewAngles[PITCH];
	viewAngles[YAW] = AngleDelta( cent_lerpAngles[YAW], angles[YAW] );
	//*tYawAngle = viewAngles[YAW];

#if 1
	if ( !BG_FlippingAnim( cent->legsAnim ) &&
		!BG_SpinningSaberAnim( cent->legsAnim ) &&
		!BG_SpinningSaberAnim( cent->torsoAnim ) &&
		!BG_InSpecialJump( cent->legsAnim ) &&
		!BG_InSpecialJump( cent->torsoAnim ) &&
		!BG_InDeathAnim(cent->legsAnim) &&
		!BG_InDeathAnim(cent->torsoAnim) &&
		!BG_InRollES(cent, cent->legsAnim) &&
		!BG_InRollAnim(cent) &&
		!BG_SaberInSpecial(cent->saberMove) &&
		!BG_SaberInSpecialAttack(cent->torsoAnim) &&
		!BG_SaberInSpecialAttack(cent->legsAnim) &&

		!BG_InKnockDown(cent->torsoAnim) &&
		!BG_InKnockDown(cent->legsAnim) &&
		!BG_InKnockDown(ciTorso) &&
		!BG_InKnockDown(ciLegs) &&

		!BG_FlippingAnim( ciLegs ) &&
		!BG_SpinningSaberAnim( ciLegs ) &&
		!BG_SpinningSaberAnim( ciTorso ) &&
		!BG_InSpecialJump( ciLegs ) &&
		!BG_InSpecialJump( ciTorso ) &&
		!BG_InDeathAnim(ciLegs) &&
		!BG_InDeathAnim(ciTorso) &&
		!BG_SaberInSpecialAttack(ciTorso) &&
		!BG_SaberInSpecialAttack(ciLegs) &&

		!(cent->eFlags & EF_DEAD) &&
		(cent->legsAnim) != (cent->torsoAnim) &&
		(ciLegs) != (ciTorso) &&
		!cent->m_iVehicleNum)
	{
		doCorr = qtrue;
	}
#else
	if ( ((!BG_FlippingAnim( cent->legsAnim )
		&& !BG_SpinningSaberAnim( cent->legsAnim )
		&& !BG_SpinningSaberAnim( cent->torsoAnim )
		&& (cent->legsAnim) != (cent->torsoAnim)) //NOTE: presumes your legs & torso are on the same frame, though they *should* be because PM_SetAnimFinal tries to keep them in synch
		||
		(!BG_FlippingAnim( ciLegs )
		&& !BG_SpinningSaberAnim( ciLegs )
		&& !BG_SpinningSaberAnim( ciTorso )
		&& (ciLegs) != (ciTorso)))
		||
		ciLegs != cent->legsAnim
		||
		ciTorso != cent->torsoAnim)
	{
		doCorr = qtrue;
		*corrTime = time + 1000; //continue correcting for a second after to smooth things out. SP doesn't need this for whatever reason but I can't find a way around it.
	}
	else if (*corrTime >= time)
	{
		if (!BG_FlippingAnim( cent->legsAnim )
			&& !BG_SpinningSaberAnim( cent->legsAnim )
			&& !BG_SpinningSaberAnim( cent->torsoAnim )
			&& !BG_FlippingAnim( ciLegs )
			&& !BG_SpinningSaberAnim( ciLegs )
			&& !BG_SpinningSaberAnim( ciTorso ))
		{
			doCorr = qtrue;
		}
	}
#endif

	if (doCorr)
	{//FIXME: no need to do this if legs and torso on are same frame
		//adjust for motion offset
		mdxaBone_t	boltMatrix;
		vec3_t		motionFwd, motionAngles;
		vec3_t		motionRt, tempAng;
		int			ang;

		trap->G2API_GetBoltMatrix_NoRecNoRot( ghoul2, 0, motionBolt, &boltMatrix, vec3_origin, cent_lerpOrigin, time, 0, modelScale);
		//BG_GiveMeVectorFromMatrix( &boltMatrix, NEGATIVE_Y, motionFwd );
		motionFwd[0] = -boltMatrix.matrix[0][1];
		motionFwd[1] = -boltMatrix.matrix[1][1];
		motionFwd[2] = -boltMatrix.matrix[2][1];

		vectoangles( motionFwd, motionAngles );

		//BG_GiveMeVectorFromMatrix( &boltMatrix, NEGATIVE_X, motionRt );
		motionRt[0] = -boltMatrix.matrix[0][0];
		motionRt[1] = -boltMatrix.matrix[1][0];
		motionRt[2] = -boltMatrix.matrix[2][0];

		vectoangles( motionRt, tempAng );
		motionAngles[ROLL] = -tempAng[PITCH];

		for ( ang = 0; ang < 3; ang++ )
		{
			viewAngles[ang] = AngleNormalize180( viewAngles[ang] - AngleNormalize180( motionAngles[ang] ) );
		}
	}

	//distribute the angles differently up the spine
	//NOTE: each of these distributions must add up to 1.0f
#ifdef _CGAME
	if ( PM_InJAPlusLedgeMove(cent->legsAnim) )
	{
		thoracicAngles[PITCH] = 0;
		llAngles[PITCH] = 0;
		ulAngles[PITCH] = 0;
	}
	else
#endif
	{
		thoracicAngles[PITCH] = viewAngles[PITCH]*0.20f;
		llAngles[PITCH] = viewAngles[PITCH]*0.40f;
		ulAngles[PITCH] = viewAngles[PITCH]*0.40f;
	}

	thoracicAngles[YAW] = viewAngles[YAW]*0.20f;
	ulAngles[YAW] = viewAngles[YAW]*0.35f;
	llAngles[YAW] = viewAngles[YAW]*0.45f;

	thoracicAngles[ROLL] = viewAngles[ROLL]*0.20f;
	ulAngles[ROLL] = viewAngles[ROLL]*0.35f;
	llAngles[ROLL] = viewAngles[ROLL]*0.45f;
}
