//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "cf_bot.h"
#include "armament.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------------------------------------------------------------------------
/**
 * Begin attacking
 */
void AttackState::OnEnter( CCFBot *me )
{
	// store our posture when the attack began
	me->PushPostureContext();

	me->DestroyPath();

	me->GetOffLadder();
	me->ResetStuckMonitor();

	m_repathTimer.Invalidate();
	m_haveSeenEnemy = me->IsEnemyVisible();
	m_nextDodgeStateTimestamp = 0.0f;
	m_firstDodge = true;
	m_isEnemyHidden = false;
	m_reacquireTimestamp = 0.0f;

	m_pinnedDownTimestamp = gpGlobals->curtime + RandomFloat( 7.0f, 10.0f );

	m_shieldToggleTimestamp = gpGlobals->curtime + RandomFloat( 2.0f, 10.0f );
	m_shieldForceOpen = false;

	// if we encountered someone while escaping, grab our weapon and fight!
	if (me->IsEscapingFromBomb())
		me->EquipBestWeapon();

	m_scopeTimestamp = 0;
	m_didAmbushCheck = false;

	float skill = me->GetProfile()->GetSkill();

	// tendency to dodge is proportional to skill
	float dodgeChance = 80.0f * skill;

	// high skill bots always dodge if outnumbered, or they see a sniper
	if (skill > 0.5f && (me->IsOutnumbered() || me->CanSeeSniper()))
	{
		dodgeChance = 100.0f;
	}

	m_shouldDodge = (RandomFloat( 0, 100 ) <= dodgeChance);


	// decide whether we might bail out of this fight
	m_isCoward = (RandomFloat( 0, 100 ) > 100.0f * me->GetProfile()->GetAggression());
}

//--------------------------------------------------------------------------------------------------------------
/**
 * When we are done attacking, this is invoked
 */
void AttackState::StopAttacking( CCFBot *me )
{
	if (me->GetTask() == CCFBot::SNIPING)
	{
		// stay in our hiding spot
		me->Hide( me->GetLastKnownArea(), -1.0f, 50.0f );
	}
	else
	{
		me->StopAttacking();
	}
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Do dodge behavior
 */
void AttackState::Dodge( CCFBot *me )
{
	//
	// Dodge.
	// If sniping or crouching, stand still.
	//
	if (m_shouldDodge && !me->IsUsingSniperRifle())
	{
		CBasePlayer *enemy = me->GetBotEnemy();
		if (enemy == NULL)
		{
			return;
		}

		Vector toEnemy = enemy->GetAbsOrigin() - me->GetAbsOrigin();
		float range = toEnemy.Length();

		const float hysterisRange = 125.0f;		// (+/-) m_combatRange

		float minRange = me->GetCombatRange() - hysterisRange;
		float maxRange = me->GetCombatRange() + hysterisRange;

		if (me->IsUsingPrimaryMelee() && !me->m_pCurrentArmament->HasBindableCombo() && !me->HasBackupFirearm())
		{
			// dodge when far away if armed only with a melee
			maxRange = 999999.9f;
		}

		// move towards (or away from) enemy if we are using a knife, behind a corner, or we aren't very skilled
		if (me->GetProfile()->GetSkill() < 0.66f || !me->IsEnemyVisible())
		{
			if (range > maxRange)
				me->MoveForward();
			else if (range < minRange)
				me->MoveBackward();
		}

		// don't dodge if enemy is facing away
		const float dodgeRange = 2000.0f;
		if (!me->CanSeeSniper() && (range > dodgeRange || !me->IsPlayerFacingMe( enemy )))
		{
			m_dodgeState = STEADY_ON;
			m_nextDodgeStateTimestamp = 0.0f;
		}
		else if (gpGlobals->curtime >= m_nextDodgeStateTimestamp)
		{
			int next;

			// high-skill bots keep moving and don't jump if they see a sniper
			if (me->GetProfile()->GetSkill() > 0.5f && me->CanSeeSniper())
			{
				// juke back and forth
				if (m_firstDodge)
				{
					next = (RandomInt( 0, 100 ) < 50) ? SLIDE_RIGHT : SLIDE_LEFT;
				}
				else
				{
					next = (m_dodgeState == SLIDE_LEFT) ? SLIDE_RIGHT : SLIDE_LEFT;
				}
			}
			else
			{
				// select next dodge state that is different that our current one
				do
				{
					// high-skill bots may jump when first engaging the enemy (if they are moving)
					const float jumpChance = 33.3f;
					if (m_firstDodge && me->GetProfile()->GetSkill() > 0.5f && RandomFloat( 0, 100 ) < jumpChance && !me->IsNotMoving())
						next = RandomInt( 0, NUM_ATTACK_STATES-1 );
					else
						next = RandomInt( 0, NUM_ATTACK_STATES-2 );
				}
				while( !m_firstDodge && next == m_dodgeState );
			}

			m_dodgeState = (DodgeStateType)next;
			m_nextDodgeStateTimestamp = gpGlobals->curtime + RandomFloat( 0.3f, 1.0f );
			m_firstDodge = false;
		}


		Vector forward, right;
		me->EyeVectors( &forward, &right );

		const float lookAheadRange = 30.0f;
		float ground;

		switch( m_dodgeState )
		{
			case STEADY_ON:
			{
				break;
			}

			case SLIDE_LEFT:
			{
				// don't move left if we will fall
				Vector pos = me->GetAbsOrigin() - (lookAheadRange * right);

				if (me->GetSimpleGroundHeightWithFloor( pos, &ground ))
				{
					if (me->GetAbsOrigin().z - ground < StepHeight)
					{
						me->StrafeLeft();
					}
				}
				break;
			}

			case SLIDE_RIGHT:
			{
				// don't move left if we will fall
				Vector pos = me->GetAbsOrigin() + (lookAheadRange * right);

				if (me->GetSimpleGroundHeightWithFloor( pos, &ground ))
				{
					if (me->GetAbsOrigin().z - ground < StepHeight)
					{
						me->StrafeRight();
					}
				}
				break;
			}

			case JUMP:
			{
				if (me->m_isEnemyVisible)
				{
					me->Jump();
				}
				break;
			}
		}
	}
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Perform attack behavior
 */
void AttackState::OnUpdate( CCFBot *me )
{
	// can't be stuck while attacking
	me->ResetStuckMonitor();

	CBasePlayer *enemy = me->GetBotEnemy();
	if (enemy == NULL)
	{
		StopAttacking( me );
		return;
	}

	Vector myOrigin = me->GetCentroid();
	Vector enemyOrigin = ToCFPlayer(enemy)->GetCentroid();

	// keep track of whether we have seen our enemy at least once yet
	if (!m_haveSeenEnemy)
		m_haveSeenEnemy = me->IsEnemyVisible();


	//
	// Retreat check
	// Do not retreat if the enemy is too close
	//
	if (m_retreatTimer.IsElapsed())
	{
		// If we've been fighting this battle for awhile, we're "pinned down" and
		// need to do something else.
		// If we are outnumbered, retreat.
		// If we see a sniper and we aren't a sniper, retreat.

		bool isPinnedDown = (gpGlobals->curtime > m_pinnedDownTimestamp);

		if (isPinnedDown || 
			(me->CanSeeSniper() && !me->IsSniper()) ||
			(me->IsOutnumbered() && m_isCoward) || 
			(me->OutnumberedCount() >= 2 && me->GetProfile()->GetAggression() < 1.0f))
		{
			// only retreat if at least one of them is aiming at me
			if (me->IsAnyVisibleEnemyLookingAtMe( true ))
			{
				// tell our teammates our plight
				if (isPinnedDown)
					me->GetChatter()->PinnedDown();
				else if (!me->CanSeeSniper())
					me->GetChatter()->Scared();

				m_retreatTimer.Start( RandomFloat( 3.0f, 15.0f ) );

				// try to retreat
				if (me->TryToRetreat())
				{
					// if we are a sniper, equip our pistol so we can fire while retreating
					/*
					if (me->IsUsingSniperRifle())
					{
						// wait a moment to allow one last shot
						me->Wait( 0.5f );
						//me->EquipPistol();
					}
					*/

					// request backup if outnumbered
					if (me->IsOutnumbered())
					{
						me->GetChatter()->NeedBackup();
					}
				}
				else	
				{
					me->PrintIfWatched( "I want to retreat, but no safe spots nearby!\n" );
				}
			}
		}
	}

	//
	// Knife fighting
	// We need to pathfind right to the enemy to cut him
	//
	if (me->IsUsingPrimaryMelee())
	{
		me->StandUp();

		// if we are using a knife and our prey is looking towards us, run at him
		if (me->IsPlayerFacingMe( enemy ))
		{
			me->ForceRun( 5.0f );
			me->Hurry( 10.0f );
		}

		if ((enemy->GetAbsOrigin() - me->GetAbsOrigin()).IsLengthLessThan(200))
		{
			// If the enemy is close up and I have a backup firearm, put it away so I can use the strong attacks.
			if (me->HasBackupFirearm())
				me->Weapon_SwitchSecondaries();
		}
		else
		{
			// If the enemy is far away and I have a backup firearm, take it out so I can shoot them.
			if (me->GetInactiveSecondaryWeapon())
				me->Weapon_SwitchSecondaries();
		}

		// slash our victim
		me->FireWeaponAtEnemy();

		// if toe to toe with our enemy, don't dodge, just slash
		const float slashRange = 70.0f;
		if ((enemy->GetAbsOrigin() - me->GetAbsOrigin()).IsLengthGreaterThan( slashRange ))
		{
			const float repathInterval = 0.5f;

			// if our victim has moved, repath
			bool repath = false;
			if (me->HasPath())
			{
				const float repathRange = 100.0f;		// 50
				if ((me->GetPathEndpoint() - enemy->GetAbsOrigin()).IsLengthGreaterThan( repathRange ))
				{
					repath = true;
				}
			}
			else
			{
				repath = true;
			}

			if (repath && m_repathTimer.IsElapsed())
			{
				Vector enemyPos = enemy->GetAbsOrigin() + Vector( 0, 0, HalfHumanHeight );
				me->ComputePath( enemyPos, FASTEST_ROUTE );
				m_repathTimer.Start( repathInterval );
			}

			// move towards victim
			if (me->UpdatePathMovement( NO_SPEED_CHANGE ) != CCFBot::PROGRESSING)
			{
				me->DestroyPath();
			}
		}

		return;
	}



	// if we're sniping, look through the scope - need to do this here in case a reload resets our scope
	if (me->IsUsingSniperRifle())
	{
		Vector toAimSpot3D = me->m_aimSpot - myOrigin;
		float targetRange = toAimSpot3D.Length();

		// dont adjust zoom level if we're already zoomed in - just fire
		if (me->GetZoomLevel() == CCFBot::NO_ZOOM && me->AdjustZoom( targetRange ))
			m_scopeTimestamp = gpGlobals->curtime;
	
		const float waitScopeTime = 0.3f + me->GetProfile()->GetReactionTime();
		if (gpGlobals->curtime - m_scopeTimestamp < waitScopeTime)
		{
			// force us to wait until zoomed in before firing
			return;
		}
	}

	// see if we "notice" that our prey is dead
	if (me->IsAwareOfEnemyDeath())
	{
		// let team know if we killed the last enemy
		if (me->GetLastVictimID() == enemy->entindex() && me->GetNearbyEnemyCount() <= 1)
		{
			me->GetChatter()->KilledMyEnemy( enemy->entindex() );

			// if there are other enemies left, wait a moment - they usually come in groups
			if (me->GetEnemiesRemaining())
			{
				me->Wait( RandomFloat( 1.0f, 3.0f ) );
			}
		}

		StopAttacking( me );
		return;
	}

	float notSeenEnemyTime = gpGlobals->curtime - me->GetLastSawEnemyTimestamp();

	// if we haven't seen our enemy for a moment, continue on if we dont want to fight, or decide to ambush if we do
	if (!me->IsEnemyVisible())
	{
		// attend to nearby enemy gunfire
		if (notSeenEnemyTime > 0.5f && me->CanHearNearbyEnemyGunfire())
		{
			// give up the attack, since we didn't want it in the first place
			StopAttacking( me );

			const Vector *pos = me->GetNoisePosition();
			if (pos)
			{
				me->SetLookAt( "Nearby enemy gunfire", *pos, PRIORITY_HIGH, 0.0f );
				me->PrintIfWatched( "Checking nearby threatening enemy gunfire!\n" );
				return;
			}
		}

		// check if we have lost track of our enemy during combat
		if (notSeenEnemyTime > 0.25f)
		{
			m_isEnemyHidden = true;
		}
					

		if (notSeenEnemyTime > 0.1f)
		{
			if (me->GetDisposition() == CCFBot::ENGAGE_AND_INVESTIGATE)
			{
				// decide whether we should hide and "ambush" our enemy
				if (m_haveSeenEnemy && !m_didAmbushCheck)
				{
					float hideChance = 33.3f;

					if (RandomFloat( 0.0, 100.0f ) < hideChance)
					{
						float ambushTime = RandomFloat( 3.0f, 15.0f );

						// hide in ambush nearby
						/// @todo look towards where we know enemy is
						const Vector *spot = FindNearbyRetreatSpot( me, 200.0f );
						if (spot)
						{
							me->IgnoreEnemies( 1.0f );

							me->Run();
							me->StandUp();
							me->Hide( *spot, ambushTime, true );
							return;
						}
					}

					// don't check again
					m_didAmbushCheck = true;
				}
			}
			else
			{
				// give up the attack, since we didn't want it in the first place
				StopAttacking( me );
				return;
			}
		}
	}
	else
	{
		// we can see the enemy again - reset our ambush check
		m_didAmbushCheck = false;

		// if the enemy is coming out of hiding, we need time to react
		if (m_isEnemyHidden)
		{
			m_reacquireTimestamp = gpGlobals->curtime + me->GetProfile()->GetReactionTime();
			m_isEnemyHidden = false;
		}
	}


	// if we haven't seen our enemy for a long time, chase after them
	float chaseTime = 2.0f + 2.0f * (1.0f - me->GetProfile()->GetAggression());

	// if we are sniping, be very patient
	if (me->IsUsingSniperRifle())
		chaseTime += 3.0f;
	else if (me->IsCrouching())	// if we are crouching, be a little patient
		chaseTime += 1.0f;

	// if we can't see the enemy, and have either seen him but currently lost sight of him, 
	// or haven't yet seen him, chase after him (unless we are a sniper)
	if (!me->IsEnemyVisible() && (notSeenEnemyTime > chaseTime || !m_haveSeenEnemy))
	{
		// snipers don't chase their prey - they wait for their prey to come to them
		if (me->GetTask() == CCFBot::SNIPING)
		{
			StopAttacking( me );
			return;
		}
		else
		{
			// move to last known position of enemy
			me->SetTask( CCFBot::MOVE_TO_LAST_KNOWN_ENEMY_POSITION, enemy );
			me->MoveTo( me->GetLastKnownEnemyPosition() );
			return;
		}
	}


	// if we can't see our enemy at the moment, and were shot by
	// a different visible enemy, engage them instead
	const float hurtRecentlyTime = 3.0f;
	if (!me->IsEnemyVisible() &&
		me->GetTimeSinceAttacked() < hurtRecentlyTime &&
		me->GetAttacker() &&
		me->GetAttacker() != me->GetBotEnemy())
	{
		// if we can see them, attack, otherwise panic
		if (me->IsVisible( me->GetAttacker(), true ))
		{
			me->Attack( me->GetAttacker() );
			me->PrintIfWatched( "Switching targets to retaliate against new attacker!\n" );
		}
		/*
		 * Rethink this
		else
		{
			me->Panic( me->GetAttacker() );
			me->PrintIfWatched( "Panicking from crossfire while attacking!\n" );
		}
		*/

		return;
	}

	if (gpGlobals->curtime > m_reacquireTimestamp)
		me->FireWeaponAtEnemy();


	// do dodge behavior
	Dodge( me );
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Finish attack
 */
void AttackState::OnExit( CCFBot *me )
{
	me->PrintIfWatched( "AttackState:OnExit()\n" );

	// clear any noises we heard during battle
	me->ForgetNoise();
	me->ResetStuckMonitor();

	// resume our original posture
	me->PopPostureContext();

	//me->StopAiming();
}

