// Original production CG_ClipMoveToEntities from 8cc641a, before swept-bounds rejection.
static void CG_ClipMoveToEntities_Baseline ( const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end,
							int skipNumber, int skipNumber2, int mask, trace_t *tr, qboolean g2Check, qboolean crosshairTrace ) {
	int			i, x, zd, zu;
	trace_t		trace, oldTrace;
	entityState_t	*ent;
	clipHandle_t 	cmodel;
	vec3_t		bmins, bmaxs;
	vec3_t		origin, angles;
	centity_t	*cent;
	centity_t	*ignored = NULL;

	if (skipNumber != -1 && skipNumber != ENTITYNUM_NONE)
	{
		ignored = &cg_entities[skipNumber];
	}

	for ( i = 0 ; i < cg_numSolidEntities ; i++ ) {
		cent = cg_solidEntities[ i ];
		ent = &cent->currentState;

		if ( ent->number == skipNumber || ent->number == skipNumber2 ) {
			continue;
		}

		if ( ent->number > MAX_CLIENTS &&
			 (ent->genericenemyindex-MAX_GENTITIES==cg.predictedPlayerState.clientNum || ent->genericenemyindex-MAX_GENTITIES==cg.predictedVehicleState.clientNum) )
//		if (ent->number > MAX_CLIENTS && cg.snap && ent->genericenemyindex && (ent->genericenemyindex-MAX_GENTITIES) == cg.snap->ps.clientNum)
		{ //rww - method of keeping objects from colliding in client-prediction (in case of ownership)
			continue;
		}

		if (ent->eType == ET_SPECIAL && ent->modelindex == HI_SHIELD)//Japro - Fix bad prediction for HI_SHIELD item for codemonkey
			continue;

		//JAPRO - Clientside - Duel Passthru Prediction - Start
		if (cgs.serverMod >= SVMOD_JAPLUS)
		{
			if (cg.predictedPlayerState.duelInProgress)
			{ // we are in a private duel
				if (ent->number != cg.predictedPlayerState.duelIndex && ent->eType != ET_MOVER)
				{ // we are not dueling them
				  // don't clip
					continue;
				}
			}
			else if (!crosshairTrace)
			{
				if (ent->bolt1 && ent->eType == ET_PLAYER)
				{ // we are not in a private duel, and this player is dueling don't clip
					continue;
				}
				else if (cgs.serverMod == SVMOD_JAPRO && cg.predictedPlayerState.stats[STAT_RACEMODE])
				{
					if (ent->eType == ET_MOVER) { //TR_SINCE since func_bobbings are still solid, sad hack
						if (ent->pos.trType != TR_SINE && (VectorLengthSquared(ent->pos.trDelta) || VectorLengthSquared(ent->apos.trDelta))) {//If its moving? //how to get classname clientside?
							continue; //Dont predict moving et_movers as solid..since that means they are likely func_door or func_plat.. which are nonsolid to racers serverside
						}
					}
					else {
						continue;
					}
				}
			}
		}
		//JAPRO - Clientside - Duel Passthru Prediction - End

		//JAPRO - Clientside - Nonsolid doors for racemode people
		/*
		if (cgs.serverMod == SVMOD_JAPRO && cg.predictedPlayerState.stats[STAT_RACEMODE]) {
			if (ent->eType == ET_MOVER) {
				if (ent->pos.trType != TR_SINE && (VectorLengthSquared(ent->pos.trDelta) || VectorLengthSquared(ent->apos.trDelta))) //If its moving? //how to get classname clientside?
					continue; //Dont predict moving et_movers as solid..since that means they are likely func_door or func_plat.. which are nonsolid to racers serverside
			}
		}
		*/
		//JAPRO race doors END

		if ( ent->solid == SOLID_BMODEL ) {
			// special value for bmodel
			cmodel = trap->CM_InlineModel( ent->modelindex );

			VectorCopy( cent->lerpAngles, angles );
			BG_EvaluateTrajectory( &cent->currentState.pos, cg.physicsTime, origin );
		} else {
			// encoded bbox
			x = (ent->solid & 255);
			zd = ((ent->solid>>8) & 255);
			zu = ((ent->solid>>16) & 255) - 32;

			bmins[0] = bmins[1] = -x;
			bmaxs[0] = bmaxs[1] = x;
			bmins[2] = -zd;
			bmaxs[2] = zu;

			if (ent->eType == ET_NPC && ent->NPC_class == CLASS_VEHICLE &&
				cent->m_pVehicle)
			{ //try to dynamically adjust his bbox dynamically, if possible
				float *old = cent->m_pVehicle->m_vOrientation;
				cent->m_pVehicle->m_vOrientation = &cent->lerpAngles[0];
				BG_VehicleAdjustBBoxForOrientation(cent->m_pVehicle, cent->lerpOrigin, bmins, bmaxs,
											cent->currentState.number, MASK_PLAYERSOLID, NULL);
				cent->m_pVehicle->m_vOrientation = old;
			}

			cmodel = trap->CM_TempModel( bmins, bmaxs, 0 );
			VectorCopy( vec3_origin, angles );

			VectorCopy( cent->lerpOrigin, origin );
		}


		trap->CM_TransformedTrace ( &trace, start, end, mins, maxs, cmodel, mask, origin, angles, 0);
		trace.entityNum = trace.fraction != 1.0 ? ent->number : ENTITYNUM_NONE;

		if (g2Check || (ignored && ignored->currentState.m_iVehicleNum))
		{
			//keep these older variables around for a bit, incase we need to replace them in the Ghoul2 Collision check
			//or in the vehicle owner trace check
			oldTrace = *tr;
		}

		if (trace.allsolid || trace.fraction < tr->fraction) {
			trace.entityNum = ent->number;
			*tr = trace;
		} else if (trace.startsolid) {
			tr->startsolid = qtrue;

			//rww 12-02-02
			tr->entityNum = trace.entityNum = ent->number;
		}
		if ( tr->allsolid )
		{
			if (ignored && ignored->currentState.m_iVehicleNum)
			{
				trace.entityNum = ent->number;
				if (CG_VehicleClipCheck(ignored, &trace))
				{ //this isn't our vehicle, we're really stuck
					return;
				}
				else
				{ //it's alright, keep going
					trace = oldTrace;
					*tr = trace;
				}
			}
			else
			{
				return;
			}
		}

		if (g2Check)
		{
			if (trace.entityNum == ent->number && cent->ghoul2)
			{
				CG_G2TraceCollide(&trace, mins, maxs, start, end);

				if (trace.entityNum == ENTITYNUM_NONE)
				{ //g2 trace failed, so put it back where it was.
					trace = oldTrace;
					*tr = trace;
				}
			}
		}

		if (ignored && ignored->currentState.m_iVehicleNum)
		{ //see if this is the vehicle we hit
			centity_t *hit = &cg_entities[trace.entityNum];
			if (!CG_VehicleClipCheck(ignored, &trace))
			{ //looks like it
				trace = oldTrace;
				*tr = trace;
			}
			else if (hit->currentState.eType == ET_MISSILE &&
				hit->currentState.owner == ignored->currentState.number)
			{ //hack, don't hit own missiles
				trace = oldTrace;
				*tr = trace;
			}
		}
	}
}
