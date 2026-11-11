// Verbatim pre-optimization production ScanForEnemies from 6d9c5f48f338f52874a26e2e7e1266b17f28e2c4.
int ScanForEnemies(bot_state_t *bs)
{
	vec3_t a;
	float distcheck;
	float closest;
	int bestindex;
	int i;
	float hasEnemyDist = 0;
	qboolean noAttackNonJM = qfalse;
	float ourHealth = g_entities[bs->client].health;

	closest = 999999;
	i = 0;
	bestindex = -1;

	if (bs->currentEnemy)
	{ //only switch to a new enemy if he's significantly closer
		if (g_newBotAI.integer)
			hasEnemyDist = 0;
		else
			hasEnemyDist = bs->frame_Enemy_Len;
	}

	if (bs->currentEnemy && bs->currentEnemy->client &&
		bs->currentEnemy->client->ps.isJediMaster)
	{ //The Jedi Master must die.
		return -1;
	}

	if (level.gametype == GT_JEDIMASTER)
	{
		if (G_ThereIsAMaster() && !bs->cur_ps.isJediMaster)
		{ //if friendly fire is on in jedi master we can attack people that bug us
			if (!g_friendlyFire.integer)
			{
				noAttackNonJM = qtrue;
			}
			else
			{
				closest = 128; //only get mad at people if they get close enough to you to anger you, or hurt you
			}
		}
	}

	while (i <= MAX_CLIENTS)
	{
		if (i != bs->client && g_entities[i].client && !OnSameTeam(&g_entities[bs->client], &g_entities[i]) && PassStandardEnemyChecks(bs, &g_entities[i])
			&& BotPVSCheck(g_entities[i].client->ps.origin, bs->eye) && PassLovedOneCheck(bs, &g_entities[i]))
		{
			VectorSubtract(g_entities[i].client->ps.origin, bs->eye, a);
			if (g_newBotAI.integer) {
				float normalizedHealth = 0.25 + (g_entities[i].health - 1)*(1 - 0.25)/(100-1); //Range .25 to 1

				normalizedHealth += (100 - ourHealth) * 0.005; //Bring normalizedhealth closer to 1 the lower HP we ourselves are?, up to +0.5?
				if (normalizedHealth > 1)
					normalizedHealth = 1;

				distcheck = VectorLength(a) * normalizedHealth;
			}
			else
				distcheck = VectorLength(a);
			vectoangles(a, a);

			if (g_entities[i].client->ps.isJediMaster)
			{ //make us think the Jedi Master is close so we'll attack him above all
				distcheck = 1;
			}

			if (distcheck < closest &&
				(g_newBotAI.integer || (((InFieldOfVision(bs->viewangles, 90, a) && !BotMindTricked(bs->client, i)) || BotCanHear(bs, &g_entities[i], distcheck)) && OrgVisible(bs->eye, g_entities[i].client->ps.origin, -1))))
			{
				if (BotMindTricked(bs->client, i))
				{
					if (distcheck < 256 || (level.time - g_entities[i].client->dangerTime) < 100)
					{
						if (!hasEnemyDist || distcheck < (hasEnemyDist - 128))
						{ //if we have an enemy, only switch to closer if he is 128+ closer to avoid flipping out
							if (!noAttackNonJM || g_entities[i].client->ps.isJediMaster)
							{
								closest = distcheck;
								bestindex = i;
							}
						}
					}
				}
				else
				{
					if (!hasEnemyDist || distcheck < (hasEnemyDist - 128))
					{ //if we have an enemy, only switch to closer if he is 128+ closer to avoid flipping out
						if (!noAttackNonJM || g_entities[i].client->ps.isJediMaster)
						{
							closest = distcheck;
							bestindex = i;
						}
					}
				}
			}
		}
		i++;
	}

	return bestindex;
}
