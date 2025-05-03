/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// g_weapon.c

#include "g_local.h"
#include "m_player.h"


static qboolean	is_quad;
static byte		is_silenced;


void weapon_grenade_fire (edict_t *ent, qboolean held);
void Pellet_Explode(edict_t* ent, edict_t* other, cplane_t* plane, csurface_t* surf);

static void P_ProjectSource (gclient_t *client, vec3_t point, vec3_t distance, vec3_t forward, vec3_t right, vec3_t result)
{
	vec3_t	_distance;

	VectorCopy (distance, _distance);
	if (client->pers.hand == LEFT_HANDED)
		_distance[1] *= -1;
	else if (client->pers.hand == CENTER_HANDED)
		_distance[1] = 0;
	G_ProjectSource (point, _distance, forward, right, result);
}


/*
===============
PlayerNoise

Each player can have two noise objects associated with it:
a personal noise (jumping, pain, weapon firing), and a weapon
target noise (bullet wall impacts)

Monsters that don't directly see the player can move
to a noise in hopes of seeing the player from there.
===============
*/
void PlayerNoise(edict_t *who, vec3_t where, int type)
{
	edict_t		*noise;

	if (type == PNOISE_WEAPON)
	{
		if (who->client->silencer_shots)
		{
			who->client->silencer_shots--;
			return;
		}
	}

	if (deathmatch->value)
		return;

	if (who->flags & FL_NOTARGET)
		return;


	if (!who->mynoise)
	{
		noise = G_Spawn();
		noise->classname = "player_noise";
		VectorSet (noise->mins, -8, -8, -8);
		VectorSet (noise->maxs, 8, 8, 8);
		noise->owner = who;
		noise->svflags = SVF_NOCLIENT;
		who->mynoise = noise;

		noise = G_Spawn();
		noise->classname = "player_noise";
		VectorSet (noise->mins, -8, -8, -8);
		VectorSet (noise->maxs, 8, 8, 8);
		noise->owner = who;
		noise->svflags = SVF_NOCLIENT;
		who->mynoise2 = noise;
	}

	if (type == PNOISE_SELF || type == PNOISE_WEAPON)
	{
		noise = who->mynoise;
		level.sound_entity = noise;
		level.sound_entity_framenum = level.framenum;
	}
	else // type == PNOISE_IMPACT
	{
		noise = who->mynoise2;
		level.sound2_entity = noise;
		level.sound2_entity_framenum = level.framenum;
	}

	VectorCopy (where, noise->s.origin);
	VectorSubtract (where, noise->maxs, noise->absmin);
	VectorAdd (where, noise->maxs, noise->absmax);
	noise->teleport_time = level.time;
	gi.linkentity (noise);
}


qboolean Pickup_Weapon (edict_t *ent, edict_t *other)
{
	int			index;
	gitem_t		*ammo;

	index = ITEM_INDEX(ent->item);

	if ( ( ((int)(dmflags->value) & DF_WEAPONS_STAY) || coop->value) 
		&& other->client->pers.inventory[index])
	{
		if (!(ent->spawnflags & (DROPPED_ITEM | DROPPED_PLAYER_ITEM) ) )
			return false;	// leave the weapon for others to pickup
	}

	other->client->pers.inventory[index]++;

	if (!(ent->spawnflags & DROPPED_ITEM) )
	{
		// give them some ammo with it
		ammo = FindItem (ent->item->ammo);
		if ( (int)dmflags->value & DF_INFINITE_AMMO )
			Add_Ammo (other, ammo, 1000);
		else
			Add_Ammo (other, ammo, ammo->quantity);

		if (! (ent->spawnflags & DROPPED_PLAYER_ITEM) )
		{
			if (deathmatch->value)
			{
				if ((int)(dmflags->value) & DF_WEAPONS_STAY)
					ent->flags |= FL_RESPAWN;
				else
					SetRespawn (ent, 30);
			}
			if (coop->value)
				ent->flags |= FL_RESPAWN;
		}
	}

	if (other->client->pers.weapon != ent->item && 
		(other->client->pers.inventory[index] == 1) &&
		( !deathmatch->value || other->client->pers.weapon == FindItem("blaster") ) )
		other->client->newweapon = ent->item;

	return true;
}


/*
===============
ChangeWeapon

The old weapon has been dropped all the way, so make the new one
current
===============
*/
void ChangeWeapon (edict_t *ent)
{
	int i;

	if (ent->client->grenade_time)
	{
		ent->client->grenade_time = level.time;
		ent->client->weapon_sound = 0;
		weapon_grenade_fire (ent, false);
		ent->client->grenade_time = 0;
	}

	ent->client->pers.lastweapon = ent->client->pers.weapon;
	ent->client->pers.weapon = ent->client->newweapon;
	ent->client->newweapon = NULL;
	ent->client->machinegun_shots = 0;

	// set visible model
	if (ent->s.modelindex == 255) {
		if (ent->client->pers.weapon)
			i = ((ent->client->pers.weapon->weapmodel & 0xff) << 8);
		else
			i = 0;
		ent->s.skinnum = (ent - g_edicts - 1) | i;
	}

	if (ent->client->pers.weapon && ent->client->pers.weapon->ammo)
		ent->client->ammo_index = ITEM_INDEX(FindItem(ent->client->pers.weapon->ammo));
	else
		ent->client->ammo_index = 0;

	if (!ent->client->pers.weapon)
	{	// dead
		ent->client->ps.gunindex = 0;
		return;
	}

	ent->client->weaponstate = WEAPON_ACTIVATING;
	ent->client->ps.gunframe = 0;
	ent->client->ps.gunindex = gi.modelindex(ent->client->pers.weapon->view_model);

	ent->client->anim_priority = ANIM_PAIN;
	if(ent->client->ps.pmove.pm_flags & PMF_DUCKED)
	{
			ent->s.frame = FRAME_crpain1;
			ent->client->anim_end = FRAME_crpain4;
	}
	else
	{
			ent->s.frame = FRAME_pain301;
			ent->client->anim_end = FRAME_pain304;
			
	}
}

/*
=================
NoAmmoWeaponChange
=================
*/
void NoAmmoWeaponChange (edict_t *ent)
{
	if ( ent->client->pers.inventory[ITEM_INDEX(FindItem("slugs"))]
		&&  ent->client->pers.inventory[ITEM_INDEX(FindItem("railgun"))] )
	{
		ent->client->newweapon = FindItem ("railgun");
		return;
	}
	if ( ent->client->pers.inventory[ITEM_INDEX(FindItem("cells"))]
		&&  ent->client->pers.inventory[ITEM_INDEX(FindItem("hyperblaster"))] )
	{
		ent->client->newweapon = FindItem ("hyperblaster");
		return;
	}
	if ( ent->client->pers.inventory[ITEM_INDEX(FindItem("bullets"))]
		&&  ent->client->pers.inventory[ITEM_INDEX(FindItem("chaingun"))] )
	{
		ent->client->newweapon = FindItem ("chaingun");
		return;
	}
	if ( ent->client->pers.inventory[ITEM_INDEX(FindItem("bullets"))]
		&&  ent->client->pers.inventory[ITEM_INDEX(FindItem("machinegun"))] )
	{
		ent->client->newweapon = FindItem ("machinegun");
		return;
	}
	if ( ent->client->pers.inventory[ITEM_INDEX(FindItem("shells"))] > 1
		&&  ent->client->pers.inventory[ITEM_INDEX(FindItem("super shotgun"))] )
	{
		ent->client->newweapon = FindItem ("super shotgun");
		return;
	}
	if ( ent->client->pers.inventory[ITEM_INDEX(FindItem("shells"))]
		&&  ent->client->pers.inventory[ITEM_INDEX(FindItem("shotgun"))] )
	{
		ent->client->newweapon = FindItem ("shotgun");
		return;
	}
	ent->client->newweapon = FindItem ("blaster");
}

/*
=================
Think_Weapon

Called by ClientBeginServerFrame and ClientThink
=================
*/
void Think_Weapon (edict_t *ent)
{
	// if just died, put the weapon away
	if (ent->health < 1)
	{
		ent->client->newweapon = NULL;
		ChangeWeapon (ent);
	}

	// call active weapon think routine
	if (ent->client->pers.weapon && ent->client->pers.weapon->weaponthink)
	{
		is_quad = (ent->client->quad_framenum > level.framenum);
		if (ent->client->silencer_shots)
			is_silenced = MZ_SILENCED;
		else
			is_silenced = 0;
		ent->client->pers.weapon->weaponthink (ent);
	}
}


/*
================
Use_Weapon

Make the weapon ready if there is ammo
================
*/
void Use_Weapon (edict_t *ent, gitem_t *item)
{
	int			ammo_index;
	gitem_t		*ammo_item;

	// see if we're already using it
	if (item == ent->client->pers.weapon)
		return;

	if (item->ammo && !g_select_empty->value && !(item->flags & IT_AMMO))
	{
		ammo_item = FindItem(item->ammo);
		ammo_index = ITEM_INDEX(ammo_item);

		if (!ent->client->pers.inventory[ammo_index])
		{
			gi.cprintf (ent, PRINT_HIGH, "No %s for %s.\n", ammo_item->pickup_name, item->pickup_name);
			return;
		}

		if (ent->client->pers.inventory[ammo_index] < item->quantity)
		{
			gi.cprintf (ent, PRINT_HIGH, "Not enough %s for %s.\n", ammo_item->pickup_name, item->pickup_name);
			return;
		}
	}

	// change to this weapon when down
	ent->client->newweapon = item;
}



/*
================
Drop_Weapon
================
*/
void Drop_Weapon (edict_t *ent, gitem_t *item)
{
	int		index;

	if ((int)(dmflags->value) & DF_WEAPONS_STAY)
		return;

	index = ITEM_INDEX(item);
	// see if we're already using it
	if ( ((item == ent->client->pers.weapon) || (item == ent->client->newweapon))&& (ent->client->pers.inventory[index] == 1) )
	{
		gi.cprintf (ent, PRINT_HIGH, "Can't drop current weapon\n");
		return;
	}

	Drop_Item (ent, item);
	ent->client->pers.inventory[index]--;
}


/*
================
Weapon_Generic

A generic function to handle the basics of weapon thinking
================
*/
#define FRAME_FIRE_FIRST		(FRAME_ACTIVATE_LAST + 1)
#define FRAME_IDLE_FIRST		(FRAME_FIRE_LAST + 1)
#define FRAME_DEACTIVATE_FIRST	(FRAME_IDLE_LAST + 1)

void Weapon_Generic (edict_t *ent, int FRAME_ACTIVATE_LAST, int FRAME_FIRE_LAST, int FRAME_IDLE_LAST, int FRAME_DEACTIVATE_LAST, int *pause_frames, int *fire_frames, void (*fire)(edict_t *ent))
{
	int		n;

	if(ent->deadflag || ent->s.modelindex != 255) // VWep animations screw up corpses
	{
		return;
	}

	if (ent->client->weaponstate == WEAPON_DROPPING)
	{
		if (ent->client->ps.gunframe == FRAME_DEACTIVATE_LAST)
		{
			ChangeWeapon (ent);
			return;
		}
		else if ((FRAME_DEACTIVATE_LAST - ent->client->ps.gunframe) == 4)
		{
			ent->client->anim_priority = ANIM_REVERSE;
			if(ent->client->ps.pmove.pm_flags & PMF_DUCKED)
			{
				ent->s.frame = FRAME_crpain4+1;
				ent->client->anim_end = FRAME_crpain1;
			}
			else
			{
				ent->s.frame = FRAME_pain304+1;
				ent->client->anim_end = FRAME_pain301;
				
			}
		}

		ent->client->ps.gunframe++;
		return;
	}

	if (ent->client->weaponstate == WEAPON_ACTIVATING)
	{
		if (ent->client->ps.gunframe == FRAME_ACTIVATE_LAST)
		{
			ent->client->weaponstate = WEAPON_READY;
			ent->client->ps.gunframe = FRAME_IDLE_FIRST;
			return;
		}

		ent->client->ps.gunframe++;
		return;
	}

	if ((ent->client->newweapon) && (ent->client->weaponstate != WEAPON_FIRING))
	{
		ent->client->weaponstate = WEAPON_DROPPING;
		ent->client->ps.gunframe = FRAME_DEACTIVATE_FIRST;

		if ((FRAME_DEACTIVATE_LAST - FRAME_DEACTIVATE_FIRST) < 4)
		{
			ent->client->anim_priority = ANIM_REVERSE;
			if(ent->client->ps.pmove.pm_flags & PMF_DUCKED)
			{
				ent->s.frame = FRAME_crpain4+1;
				ent->client->anim_end = FRAME_crpain1;
			}
			else
			{
				ent->s.frame = FRAME_pain304+1;
				ent->client->anim_end = FRAME_pain301;
				
			}
		}
		return;
	}

	if (ent->client->weaponstate == WEAPON_READY)
	{
		if ( ((ent->client->latched_buttons|ent->client->buttons) & BUTTON_ATTACK) )
		{
			ent->client->latched_buttons &= ~BUTTON_ATTACK;
			if ((!ent->client->ammo_index) || 
				( ent->client->pers.inventory[ent->client->ammo_index] >= ent->client->pers.weapon->quantity))
			{
				ent->client->ps.gunframe = FRAME_FIRE_FIRST;
				ent->client->weaponstate = WEAPON_FIRING;

				// start the animation
				ent->client->anim_priority = ANIM_ATTACK;
				if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
				{
					ent->s.frame = FRAME_crattak1-1;
					ent->client->anim_end = FRAME_crattak9;
				}
				else
				{
					ent->s.frame = FRAME_attack1-1;
					ent->client->anim_end = FRAME_attack8;
				}
			}
			else
			{
				if (level.time >= ent->pain_debounce_time)
				{
					gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/noammo.wav"), 1, ATTN_NORM, 0);
					ent->pain_debounce_time = level.time + 1;
				}
				NoAmmoWeaponChange (ent);
			}
		}
		else
		{
			if (ent->client->ps.gunframe == FRAME_IDLE_LAST)
			{
				ent->client->ps.gunframe = FRAME_IDLE_FIRST;
				return;
			}

			if (pause_frames)
			{
				for (n = 0; pause_frames[n]; n++)
				{
					if (ent->client->ps.gunframe == pause_frames[n])
					{
						if (rand()&15)
							return;
					}
				}
			}

			ent->client->ps.gunframe++;
			return;
		}
	}

	if (ent->client->weaponstate == WEAPON_FIRING)
	{
		for (n = 0; fire_frames[n]; n++)
		{
			if (ent->client->ps.gunframe == fire_frames[n])
			{
				if (ent->client->quad_framenum > level.framenum)
					gi.sound(ent, CHAN_ITEM, gi.soundindex("items/damage3.wav"), 1, ATTN_NORM, 0);

				fire (ent);
				break;
			}
		}

		if (!fire_frames[n])
			ent->client->ps.gunframe++;

		if (ent->client->ps.gunframe == FRAME_IDLE_FIRST+1)
			ent->client->weaponstate = WEAPON_READY;
	}
}


/*
======================================================================

GRENADE

======================================================================
*/

#define GRENADE_TIMER		3.0
#define GRENADE_MINSPEED	400
#define GRENADE_MAXSPEED	800

void weapon_grenade_fire (edict_t *ent, qboolean held)
{
	vec3_t	offset;
	vec3_t	forward, right;
	vec3_t	start;
	int		damage = 25;
	float	timer;
	int		speed;
	float	radius;

	radius = damage+40;
	if (is_quad)
		damage *= 4;

	VectorSet(offset, 8, 8, ent->viewheight-8);
	AngleVectors (ent->client->v_angle, forward, right, NULL);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);

	timer = ent->client->grenade_time - level.time;
	speed = GRENADE_MINSPEED + (GRENADE_TIMER - timer) * ((GRENADE_MAXSPEED - GRENADE_MINSPEED) / GRENADE_TIMER);
	fire_grenade2 (ent, start, forward, damage, speed, timer, radius, held);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		ent->client->pers.inventory[ent->client->ammo_index]--;

	ent->client->grenade_time = level.time + 1.0;

	if(ent->deadflag || ent->s.modelindex != 255) // VWep animations screw up corpses
	{
		return;
	}

	if (ent->health <= 0)
		return;

	if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
	{
		ent->client->anim_priority = ANIM_ATTACK;
		ent->s.frame = FRAME_crattak1-1;
		ent->client->anim_end = FRAME_crattak3;
	}
	else
	{
		ent->client->anim_priority = ANIM_REVERSE;
		ent->s.frame = FRAME_wave08;
		ent->client->anim_end = FRAME_wave01;
	}
}

void Weapon_Grenade (edict_t *ent)
{
	if ((ent->client->newweapon) && (ent->client->weaponstate == WEAPON_READY))
	{
		ChangeWeapon (ent);
		return;
	}

	if (ent->client->weaponstate == WEAPON_ACTIVATING)
	{
		ent->client->weaponstate = WEAPON_READY;
		ent->client->ps.gunframe = 16;
		return;
	}

	if (ent->client->weaponstate == WEAPON_READY)
	{
		if ( ((ent->client->latched_buttons|ent->client->buttons) & BUTTON_ATTACK) )
		{
			ent->client->latched_buttons &= ~BUTTON_ATTACK;
			if (ent->client->pers.inventory[ent->client->ammo_index])
			{
				ent->client->ps.gunframe = 1;
				ent->client->weaponstate = WEAPON_FIRING;
				ent->client->grenade_time = 0;
			}
			else
			{
				if (level.time >= ent->pain_debounce_time)
				{
					gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/noammo.wav"), 1, ATTN_NORM, 0);
					ent->pain_debounce_time = level.time + 1;
				}
				NoAmmoWeaponChange (ent);
			}
			return;
		}

		if ((ent->client->ps.gunframe == 29) || (ent->client->ps.gunframe == 34) || (ent->client->ps.gunframe == 39) || (ent->client->ps.gunframe == 48))
		{
			if (rand()&15)
				return;
		}

		if (++ent->client->ps.gunframe > 48)
			ent->client->ps.gunframe = 16;
		return;
	}

	if (ent->client->weaponstate == WEAPON_FIRING)
	{
		if (ent->client->ps.gunframe == 5)
			gi.sound(ent, CHAN_WEAPON, gi.soundindex("weapons/hgrena1b.wav"), 1, ATTN_NORM, 0);

		if (ent->client->ps.gunframe == 11)
		{
			if (!ent->client->grenade_time)
			{
				ent->client->grenade_time = level.time + GRENADE_TIMER + 0.2;
				ent->client->weapon_sound = gi.soundindex("weapons/hgrenc1b.wav");
			}

			// they waited too long, detonate it in their hand
			if (!ent->client->grenade_blew_up && level.time >= ent->client->grenade_time)
			{
				ent->client->weapon_sound = 0;
				weapon_grenade_fire (ent, true);
				ent->client->grenade_blew_up = true;
			}

			if (ent->client->buttons & BUTTON_ATTACK)
				return;

			if (ent->client->grenade_blew_up)
			{
				if (level.time >= ent->client->grenade_time)
				{
					ent->client->ps.gunframe = 15;
					ent->client->grenade_blew_up = false;
				}
				else
				{
					return;
				}
			}
		}

		if (ent->client->ps.gunframe == 12)
		{
			ent->client->weapon_sound = 0;
			weapon_grenade_fire (ent, false);
		}

		if ((ent->client->ps.gunframe == 15) && (level.time < ent->client->grenade_time))
			return;

		ent->client->ps.gunframe++;

		if (ent->client->ps.gunframe == 16)
		{
			ent->client->grenade_time = 0;
			ent->client->weaponstate = WEAPON_READY;
		}
	}
}

/*
======================================================================

GRENADE LAUNCHER

======================================================================
*/

void weapon_grenadelauncher_fire(edict_t* ent)
{
	vec3_t	offset;
	vec3_t	forward, right, up;
	vec3_t	start;
	int		damage = 120;
	float	radius;
	qboolean bounce_mode = false;

	// Check if the player is crouching to make it bounce
	if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
		bounce_mode = true;

	radius = damage + 40;
	if (is_quad)
		damage *= 4;

	VectorSet(offset, 8, 8, ent->viewheight - 8);
	AngleVectors(ent->client->v_angle, forward, right, up);
	P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start);

	VectorScale(forward, -2, ent->client->kick_origin);
	ent->client->kick_angles[0] = -1;

	if (bounce_mode)
	{
		// Bouncy grenade mode - use standard fire but with different parameters
		gi.cprintf(ent, PRINT_HIGH, "Bounce mode!\n");

		// how to make it interesting
		// - Slower speed
		// - Longer timer
		// - Slight upward arc
		vec3_t adjusted_forward;

		// Add an upward to the firing angle
		VectorCopy(forward, adjusted_forward);
		adjusted_forward[2] += 0.2; // Add arc
		VectorNormalize(adjusted_forward);

		// Fire the bouncy grenade with modified parameters
		fire_grenade(ent, start, adjusted_forward, damage, 450, 5.0, radius);

		gi.sound(ent, CHAN_WEAPON, gi.soundindex("weapons/grenlb1b.wav"), 1, ATTN_NORM, 0);
	}
	else
	{
		// Normal fire
		fire_grenade(ent, start, forward, damage, 600, 2.5, radius);
	}

	gi.WriteByte(svc_muzzleflash);
	gi.WriteShort(ent - g_edicts);
	gi.WriteByte(MZ_GRENADE | is_silenced);
	gi.multicast(ent->s.origin, MULTICAST_PVS);

	ent->client->ps.gunframe++;

	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (!((int)dmflags->value & DF_INFINITE_AMMO))
		ent->client->pers.inventory[ent->client->ammo_index]--;
}

void Weapon_GrenadeLauncher (edict_t *ent)
{
	static int	pause_frames[]	= {34, 51, 59, 0};
	static int	fire_frames[]	= {6, 0};

	Weapon_Generic (ent, 5, 16, 59, 64, pause_frames, fire_frames, weapon_grenadelauncher_fire);
}

/*
======================================================================

ROCKET

======================================================================
*/

void Weapon_RocketLauncher_Fire(edict_t *ent)
{
	vec3_t	offset, start;
	vec3_t	forward, right, up;
	int		damage;
	float	damage_radius;
	int		radius_damage;

	damage = 100 + (int)(random() * 20.0);
	radius_damage = 120;
	damage_radius = 120;
	if (is_quad)
	{
		damage *= 4;
		radius_damage *= 4;
	}

	AngleVectors(ent->client->v_angle, forward, right, up);

	VectorScale(forward, -2, ent->client->kick_origin);
	ent->client->kick_angles[0] = -1;

	VectorSet(offset, 8, 8, ent->viewheight - 8);
	P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start);

	fire_rocket(ent, start, forward, damage, 650, damage_radius, radius_damage);

	// Create 2 more rockets with angle variations if enough ammo
	if (ent->client->pers.inventory[ent->client->ammo_index] >= 3 ||
		((int)dmflags->value & DF_INFINITE_AMMO))
	{
		vec3_t spread_right, spread_left;

		// Copy forward vector for right rocket but more right directionness
		VectorCopy(forward, spread_right);
		VectorMA(spread_right, 0.1, right, spread_right);  // + 10% of right vector
		VectorNormalize(spread_right);

		// Copy forward vector for left rocket with more left directionness
		VectorCopy(forward, spread_left);
		VectorMA(spread_left, -0.1, right, spread_left);   // - 10% of right vector
		VectorNormalize(spread_left);

		// Fire 2 side rockets
		fire_rocket(ent, start, spread_right, damage, 650, damage_radius, radius_damage);
		fire_rocket(ent, start, spread_left, damage, 650, damage_radius, radius_damage);

		// Use 2 more rockets
		if (!((int)dmflags->value & DF_INFINITE_AMMO))
			ent->client->pers.inventory[ent->client->ammo_index] -= 2;

		// Give feedback for debugging
		gi.cprintf(ent, PRINT_HIGH, "Tri-shot!\n");
	}

	// send muzzle flash
	gi.WriteByte(svc_muzzleflash);
	gi.WriteShort(ent - g_edicts);
	gi.WriteByte(MZ_ROCKET | is_silenced);
	gi.multicast(ent->s.origin, MULTICAST_PVS);

	ent->client->ps.gunframe++;

	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (!((int)dmflags->value & DF_INFINITE_AMMO))
		ent->client->pers.inventory[ent->client->ammo_index]--;
}

void Weapon_RocketLauncher (edict_t *ent)
{
	static int	pause_frames[]	= {25, 33, 42, 50, 0};
	static int	fire_frames[]	= {5, 0};

	Weapon_Generic (ent, 4, 12, 50, 54, pause_frames, fire_frames, Weapon_RocketLauncher_Fire);
}


/*
======================================================================

BLASTER / HYPERBLASTER

======================================================================
*/

void Blaster_Fire(edict_t* ent, vec3_t g_offset, int damage, qboolean hyper, int effect)
{
	vec3_t forward, right, start, offset;
	vec3_t end;
	trace_t tr;

	if (is_quad)
		damage *= 4;


	AngleVectors(ent->client->v_angle, forward, right, NULL);
	VectorSet(offset, 24, 8, ent->viewheight - 8);
	VectorAdd(offset, g_offset, offset);
	P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start);

	VectorMA(start, 8192, forward, end);
	tr = gi.trace(start, NULL, NULL, end, ent, MASK_SHOT);

	VectorScale(forward, -2, ent->client->kick_origin);
	ent->client->kick_angles[0] = -1;
	fire_blaster(ent, start, forward, damage, 1000, effect, hyper);

	gi.WriteByte(svc_muzzleflash);
	gi.WriteShort(ent - g_edicts);
	gi.WriteByte(hyper ? (MZ_HYPERBLASTER | is_silenced) : (MZ_BLASTER | is_silenced));
	gi.multicast(ent->s.origin, MULTICAST_PVS);

	PlayerNoise(ent, start, PNOISE_WEAPON);

	// place a box if the trace hit something
	if (tr.fraction < 1.0f)
	{
		// Slightly offset so box doesn't embed in wall
		vec3_t spawnPos;
		VectorMA(tr.endpos, -8, forward, spawnPos);

		// Spawn here
		edict_t* box = G_Spawn();
		box->classname = "buildable_box";
		VectorCopy(spawnPos, box->s.origin);
		gi.setmodel(box, "models/objects/debris2/tris.md2");
		box->movetype = MOVETYPE_TOSS;
		box->solid = SOLID_BBOX;

		// make tweaks here
		VectorSet(box->mins, -16, -16, -16);
		VectorSet(box->maxs, 16, 16, 16);

		// make it visible
		box->s.effects |= EF_COLOR_SHELL; 
		box->s.renderfx |= RF_SHELL_RED;  

		gi.linkentity(box);
	}
}


void Weapon_Blaster_Fire (edict_t *ent)
{
	int		damage;

	if (deathmatch->value)
		damage = 15;
	else
		damage = 10;
	Blaster_Fire (ent, vec3_origin, damage, false, EF_BLASTER);
	ent->client->ps.gunframe++;
}

void Weapon_Blaster (edict_t *ent)
{
	static int	pause_frames[]	= {19, 32, 0};
	static int	fire_frames[]	= {5, 0};

	Weapon_Generic (ent, 4, 8, 52, 55, pause_frames, fire_frames, Weapon_Blaster_Fire);
}


// idea: fire a ricocheting hyperblaster bolt
static void fire_hyperblaster_ricochet(edict_t* ent, vec3_t start, vec3_t dir, int damage, int effect)
{
	trace_t tr;
	vec3_t end, reflect, newdir;
	int ricochet_damage = damage / 2;

	// Trace to see what hits
	VectorMA(start, 8192, dir, end);
	tr = gi.trace(start, NULL, NULL, end, ent, MASK_SHOT);

	// first fire
	fire_blaster(ent, start, dir, damage, 1000, effect, true);

	// if wall hit
	if (tr.fraction < 1.0 && (!tr.ent || !tr.ent->takedamage))
	{
		// Reflect the direction off wall
		float dot = DotProduct(dir, tr.plane.normal);
		VectorMA(dir, -2 * dot, tr.plane.normal, reflect);
		VectorNormalize(reflect);

		// Ricochet: fire a second, weaker, blue bolt from hit point
		fire_blaster(ent, tr.endpos, reflect, ricochet_damage, 1000, EF_BLUEHYPERBLASTER, true);

		gi.sound(ent, CHAN_AUTO, gi.soundindex("world/ric1.wav"), 1, ATTN_NORM, 0);
	}
}

void Weapon_HyperBlaster_Fire(edict_t* ent)
{
	float rotation;
	vec3_t offset, forward, right, start;
	int effect, damage;
	int ammo_index = ITEM_INDEX(FindItem("cells"));

	ent->client->weapon_sound = gi.soundindex("weapons/hyprbl1a.wav");

	if (!(ent->client->buttons & BUTTON_ATTACK))
	{
		ent->client->ps.gunframe++;
		return;
	}

	if (ent->client->pers.inventory[ammo_index] < 1)
	{
		if (level.time >= ent->pain_debounce_time)
		{
			gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/noammo.wav"), 1, ATTN_NORM, 0);
			ent->pain_debounce_time = level.time + 1;
		}
		NoAmmoWeaponChange(ent);
		return;
	}

	rotation = (ent->client->ps.gunframe - 5) * 2.0f * (float)M_PI / 6.0f;
	offset[0] = -4.0f * sinf(rotation);
	offset[1] = 0.0f;
	offset[2] = 4.0f * cosf(rotation);

	if ((ent->client->ps.gunframe == 6) || (ent->client->ps.gunframe == 9))
		effect = EF_HYPERBLASTER;
	else
		effect = 0;

	damage = deathmatch->value ? 18 : 25;
	if (is_quad)
		damage *= 4;

	// calc direction here
	AngleVectors(ent->client->v_angle, forward, right, NULL);
	VectorSet(offset, 24 + offset[0], 8 + offset[1], ent->viewheight - 8 + offset[2]);
	P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start);

	// Fire with ricochet idea stuff
	fire_hyperblaster_ricochet(ent, start, forward, damage, effect);

	if (!((int)dmflags->value & DF_INFINITE_AMMO))
		ent->client->pers.inventory[ammo_index]--;

	ent->client->anim_priority = ANIM_ATTACK;
	if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
	{
		ent->s.frame = FRAME_crattak1 - 1;
		ent->client->anim_end = FRAME_crattak9;
	}
	else
	{
		ent->s.frame = FRAME_attack1 - 1;
		ent->client->anim_end = FRAME_attack8;
	}

	ent->client->ps.gunframe++;
	if (ent->client->ps.gunframe == 12 && ent->client->pers.inventory[ammo_index])
		ent->client->ps.gunframe = 6;

	if (ent->client->ps.gunframe == 12)
	{
		gi.sound(ent, CHAN_AUTO, gi.soundindex("weapons/hyprbd1a.wav"), 1, ATTN_NORM, 0);
		ent->client->weapon_sound = 0;
	}
}


void Weapon_HyperBlaster (edict_t *ent)
{
	static int	pause_frames[]	= {0};
	static int	fire_frames[]	= {6, 7, 8, 9, 10, 11, 0};

	Weapon_Generic (ent, 5, 20, 49, 53, pause_frames, fire_frames, Weapon_HyperBlaster_Fire);
}

/*
======================================================================

MACHINEGUN / CHAINGUN

======================================================================
*/

void Machinegun_Fire(edict_t *ent)
{
	int i;
	vec3_t start;
	vec3_t forward, right, up;
	vec3_t angles;
	vec3_t offset;
	vec3_t tracer_start;
	int damage = 8;
	int kick = 2;
	qboolean is_tracer = false;

	if (!(ent->client->buttons & BUTTON_ATTACK))
	{
		ent->client->machinegun_shots = 0;
		ent->client->ps.gunframe++;
		return;
	}

	if (ent->client->ps.gunframe == 5)
		ent->client->ps.gunframe = 4;
	else
		ent->client->ps.gunframe = 5;

	if (ent->client->pers.inventory[ent->client->ammo_index] < 1)
	{
		ent->client->ps.gunframe = 6;
		if (level.time >= ent->pain_debounce_time)
		{
			gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/noammo.wav"), 1, ATTN_NORM, 0);
			ent->pain_debounce_time = level.time + 1;
		}
		NoAmmoWeaponChange(ent);
		return;
	}

	if (is_quad)
	{
		damage *= 4;
		kick *= 4;
	}

	for (i = 1; i < 3; i++)
	{
		ent->client->kick_origin[i] = crandom() * 0.35;
		ent->client->kick_angles[i] = crandom() * 0.7;
	}
	ent->client->kick_origin[0] = crandom() * 0.35;
	ent->client->kick_angles[0] = ent->client->machinegun_shots * -1.5;

	// raise the gun as it is firing
	if (!deathmatch->value)
	{
		ent->client->machinegun_shots++;
		if (ent->client->machinegun_shots > 9)
			ent->client->machinegun_shots = 9;
	}

	// get start / end positions
	VectorAdd(ent->client->v_angle, ent->client->kick_angles, angles);
	AngleVectors(angles, forward, right, up);
	VectorSet(offset, 0, 8, ent->viewheight - 8);
	P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start);

	// random spread
	forward[0] += crandom() * 0.05; // Horizontal
	forward[1] += crandom() * 0.05; // Vertical


	fire_bullet(ent, start, forward, damage, kick, DEFAULT_BULLET_HSPREAD, DEFAULT_BULLET_VSPREAD, MOD_MACHINEGUN);

	if (ent->client->machinegun_shots % 3 == 0)
	{
		is_tracer = true;
		VectorCopy(start, tracer_start);
		fire_blaster(ent, tracer_start, forward, damage / 2, 1500, EF_BLASTER, false); // Tracer is a glowing projectile
	}

	gi.WriteByte(svc_muzzleflash);
	gi.WriteShort(ent - g_edicts);
	gi.WriteByte(MZ_MACHINEGUN | is_silenced);
	gi.multicast(ent->s.origin, MULTICAST_PVS);


	if (is_tracer)
	{
		gi.sound(ent, CHAN_WEAPON, gi.soundindex("weapons/rocklf1a.wav"), 1, ATTN_NORM, 0);
	}

	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (!((int)dmflags->value & DF_INFINITE_AMMO))
		ent->client->pers.inventory[ent->client->ammo_index]--;

	ent->client->anim_priority = ANIM_ATTACK;
	if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
	{
		ent->s.frame = FRAME_crattak1 - (int)(random() + 0.25);
		ent->client->anim_end = FRAME_crattak9;
	}
	else
	{
		ent->s.frame = FRAME_attack1 - (int)(random() + 0.25);
		ent->client->anim_end = FRAME_attack8;
	}
}

void Weapon_Machinegun (edict_t *ent)
{
	static int	pause_frames[]	= {23, 45, 0};
	static int	fire_frames[]	= {4, 5, 0};

	Weapon_Generic (ent, 3, 5, 45, 49, pause_frames, fire_frames, Machinegun_Fire);
}

void Chaingun_Fire(edict_t* ent)
{
	int i, shots;
	vec3_t start, forward, right, up, offset;
	float r, u;
	int damage;
	int kick = 2;

	// Overheat function idea
	static const int MAX_HEAT = 100; // max heat before overheating
	static const int HEAT_PER_SHOT = 8; // Heat added per shot
	static const int COOL_DOWN_RATE = 30; // Heat reduced per frame when not firing (increased for faster cooldown)

	// Initialize overheat here
	if (!ent->client->chaingun_heat)
		ent->client->chaingun_heat = 0;

	// Check if chaingun is overheated
	if (ent->client->chaingun_heat >= MAX_HEAT)
	{
		gi.cprintf(ent, PRINT_HIGH, "Chaingun overheated!\n");
		ent->client->ps.gunframe = 32; // Skip firing frames so it can actually cool down
		ent->client->weapon_sound = 0;
		ent->client->chaingun_heat -= COOL_DOWN_RATE; // Cool down while overheated
		if (ent->client->chaingun_heat <= 0)
			ent->client->chaingun_heat = 0; // Reset heat when fully cooled
		return;
	}

	if (deathmatch->value)
		damage = 6;
	else
		damage = 8;

	if (ent->client->ps.gunframe == 5)
		gi.sound(ent, CHAN_AUTO, gi.soundindex("weapons/chngnu1a.wav"), 1, ATTN_IDLE, 0);

	if ((ent->client->ps.gunframe == 14) && !(ent->client->buttons & BUTTON_ATTACK))
	{
		ent->client->ps.gunframe = 32;
		ent->client->weapon_sound = 0;
		return;
	}
	else if ((ent->client->ps.gunframe == 21) && (ent->client->buttons & BUTTON_ATTACK)
		&& ent->client->pers.inventory[ent->client->ammo_index])
	{
		ent->client->ps.gunframe = 15;
	}
	else
	{
		ent->client->ps.gunframe++;
	}

	if (ent->client->ps.gunframe == 22)
	{
		ent->client->weapon_sound = 0;
		gi.sound(ent, CHAN_AUTO, gi.soundindex("weapons/chngnd1a.wav"), 1, ATTN_IDLE, 0);
	}
	else
	{
		ent->client->weapon_sound = gi.soundindex("weapons/chngnl1a.wav");
	}

	ent->client->anim_priority = ANIM_ATTACK;
	if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
	{
		ent->s.frame = FRAME_crattak1 - (ent->client->ps.gunframe & 1);
		ent->client->anim_end = FRAME_crattak9;
	}
	else
	{
		ent->s.frame = FRAME_attack1 - (ent->client->ps.gunframe & 1);
		ent->client->anim_end = FRAME_attack8;
	}

	if (ent->client->ps.gunframe <= 9)
		shots = 1;
	else if (ent->client->ps.gunframe <= 14)
	{
		if (ent->client->buttons & BUTTON_ATTACK)
			shots = 2;
		else
			shots = 1;
	}
	else
		shots = 3;

	if (ent->client->pers.inventory[ent->client->ammo_index] < shots)
		shots = ent->client->pers.inventory[ent->client->ammo_index];

	if (!shots)
	{
		if (level.time >= ent->pain_debounce_time)
		{
			gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/noammo.wav"), 1, ATTN_NORM, 0);
			ent->pain_debounce_time = level.time + 1;
		}
		NoAmmoWeaponChange(ent);
		return;
	}

	if (is_quad)
	{
		damage *= 4;
		kick *= 4;
	}

	for (i = 0; i < 3; i++)
	{
		ent->client->kick_origin[i] = crandom() * 0.35;
		ent->client->kick_angles[i] = crandom() * 0.7;
	}

	for (i = 0; i < shots; i++)
	{
		// get start / end positions
		AngleVectors(ent->client->v_angle, forward, right, up);
		r = 7 + crandom() * 4;
		u = crandom() * 4;
		VectorSet(offset, 0, r, u + ent->viewheight - 8);
		P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start);

		fire_bullet(ent, start, forward, damage, kick, DEFAULT_BULLET_HSPREAD, DEFAULT_BULLET_VSPREAD, MOD_CHAINGUN);
	}

	// send muzzle flash
	gi.WriteByte(svc_muzzleflash);
	gi.WriteShort(ent - g_edicts);
	gi.WriteByte((MZ_CHAINGUN1 + shots - 1) | is_silenced);
	gi.multicast(ent->s.origin, MULTICAST_PVS);

	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (!((int)dmflags->value & DF_INFINITE_AMMO))
		ent->client->pers.inventory[ent->client->ammo_index] -= shots;

	// Add heat for each shot
	ent->client->chaingun_heat += HEAT_PER_SHOT * shots;

	// Cap heat at max
	if (ent->client->chaingun_heat > MAX_HEAT)
		ent->client->chaingun_heat = MAX_HEAT;

	if (ent->client->chaingun_heat > MAX_HEAT * 0.75)
	{
		ent->s.effects |= EF_COLOR_SHELL; // make it glow
		ent->s.renderfx |= RF_SHELL_RED;  // will be red
	}
	else
	{
		ent->s.effects &= ~EF_COLOR_SHELL;
		ent->s.renderfx &= ~RF_SHELL_RED;
	}
}


void Weapon_Chaingun (edict_t *ent)
{
	static int	pause_frames[]	= {38, 43, 51, 61, 0};
	static int	fire_frames[]	= {5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 0};

	Weapon_Generic (ent, 4, 31, 61, 64, pause_frames, fire_frames, Chaingun_Fire);
}


/*
======================================================================

SHOTGUN / SUPERSHOTGUN

======================================================================
*/

void Pellet_Explode(edict_t* ent, edict_t* other, cplane_t* plane, csurface_t* surf)
{
	// Don't explode if it hits the owner to avoid self-damage when first fired
	if (other == ent->owner)
		return;

	// If it hits the sky, just disappear
	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict(ent);
		return;
	}


	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_EXPLOSION1);
	gi.WritePosition(ent->s.origin);
	gi.multicast(ent->s.origin, MULTICAST_PVS);

	// Radius Damange: inflictor, attacker, damage, ignore entity, radius, MOA
	T_RadiusDamage(ent, ent->owner, ent->dmg, NULL, ent->dmg_radius, MOD_SHOTGUN);

	// Free entity
	G_FreeEdict(ent);
}

void fire_explosive_pellet(edict_t* self, vec3_t start, vec3_t dir, int damage, float speed, float radius)
{
	edict_t* pellet = G_Spawn();
	VectorCopy(start, pellet->s.origin);
	VectorCopy(dir, pellet->movedir);
	vectoangles(dir, pellet->s.angles);
	VectorScale(dir, speed, pellet->velocity);
	pellet->movetype = MOVETYPE_FLYMISSILE;
	pellet->clipmask = MASK_SHOT;
	pellet->solid = SOLID_BBOX;

	pellet->s.modelindex = gi.modelindex("models/objects/grenade2/tris.md2");
	pellet->owner = self;
	pellet->touch = Pellet_Explode;
	pellet->nextthink = level.time + 1.5; // Shorter lifetime to make different
	pellet->think = Pellet_Explode; // If it doesn't hit anything, explode with delay
	pellet->dmg = damage;
	pellet->dmg_radius = radius;

	pellet->s.effects |= EF_GRENADE;

	VectorSet(pellet->mins, -2, -2, -2);
	VectorSet(pellet->maxs, 2, 2, 2);

	gi.linkentity(pellet);

	gi.sound(self, CHAN_WEAPON, gi.soundindex("weapons/grenlf1a.wav"), 0.5, ATTN_NORM, 0);
}

void weapon_shotgun_fire(edict_t* ent)
{
	vec3_t start;
	vec3_t forward, right;
	vec3_t offset;
	vec3_t pellet_dir;
	int damage = 40; 
	int kick = 8;
	int pellet_count = 2;
	float spread = 0.15;
	float speed = 800;
	float radius = 40;

	// calc angles
	AngleVectors(ent->client->v_angle, forward, right, NULL);

	// making kickback here
	VectorScale(forward, -2, ent->client->kick_origin);
	ent->client->kick_angles[0] = -2;

	// calc the starting pos of pellets
	VectorSet(offset, 0, 8, ent->viewheight - 8);
	P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start);

	if (is_quad)
	{
		damage *= 4;
		kick *= 4;
	}

	// Fire multiple
	for (int i = 0; i < pellet_count; i++)
	{
		VectorCopy(forward, pellet_dir);

		// random spread here 
		pellet_dir[0] += crandom() * spread; // Horizontal
		pellet_dir[1] += crandom() * spread; // Vertical
		pellet_dir[2] += crandom() * spread * 0.5; // Vertical spread again add variation this time

		// Fire explosive pellet
		fire_explosive_pellet(ent, start, pellet_dir, damage, speed, radius);
	}

	// send muzzle flash
	gi.WriteByte(svc_muzzleflash);
	gi.WriteShort(ent - g_edicts);
	gi.WriteByte(MZ_SHOTGUN | is_silenced);
	gi.multicast(ent->s.origin, MULTICAST_PVS);

	gi.sound(ent, CHAN_WEAPON, gi.soundindex("weapons/shotgf1b.wav"), 1, ATTN_NORM, 0);

	ent->client->ps.gunframe++;
	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (!((int)dmflags->value & DF_INFINITE_AMMO))
		ent->client->pers.inventory[ent->client->ammo_index]--;
}

void Weapon_Shotgun (edict_t *ent)
{
	static int	pause_frames[]	= {22, 28, 34, 0};
	static int	fire_frames[]	= {8, 9, 0};

	Weapon_Generic (ent, 7, 18, 36, 39, pause_frames, fire_frames, weapon_shotgun_fire);
}

void CreateTrail(edict_t* slug, vec3_t start, vec3_t end)
{
	edict_t* trail = G_Spawn();
	trail->classname = "slug_trail";
	trail->movetype = MOVETYPE_NOCLIP;
	trail->solid = SOLID_NOT;
	trail->s.effects = EF_HYPERBLASTER; 
	trail->s.renderfx = RF_SHELL_GREEN;
	VectorCopy(start, trail->s.origin);
	VectorCopy(end, trail->s.old_origin);
	trail->think = G_FreeEdict;
	trail->nextthink = level.time + 0.1;
	gi.linkentity(trail);
}

void weapon_supershotgun_fire(edict_t *ent)
{
	vec3_t start;
	vec3_t forward, right;
	vec3_t offset;
	vec3_t slug_dir;
	vec3_t end;
	trace_t tr;
	int damage = 80; 
	int kick = 100; 
	int max_pierces = 3; 

	AngleVectors(ent->client->v_angle, forward, right, NULL);


	VectorScale(forward, -4, ent->client->kick_origin);
	ent->client->kick_angles[0] = -4;

	VectorSet(offset, 0, 8, ent->viewheight - 8);
	P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start);

	if (is_quad)
	{
		damage *= 4;
		kick *= 4;
	}

	for (int i = 0; i < 2; i++)
	{
		int pierces = 0;
		VectorCopy(forward, slug_dir);

		// make different spread for next shot
		if (i == 1)
			slug_dir[1] += 0.1;

		VectorMA(start, 8192, slug_dir, end); // make path longer

		while (pierces < max_pierces)
		{
			tr = gi.trace(start, NULL, NULL, end, ent, MASK_SHOT);

			if (tr.fraction < 1.0) // if it hits something
			{
				if (tr.ent && tr.ent->takedamage)
				{
					T_Damage(tr.ent, ent, ent, slug_dir, tr.endpos, vec3_origin, damage, kick, DAMAGE_BULLET, MOD_SSHOTGUN);
					pierces++;
				}

				CreateTrail(ent, start, tr.endpos);
				VectorMA(tr.endpos, 1, slug_dir, start);
			}
			else
			{
				// trail full path
				CreateTrail(ent, start, end);
				break; // Stop if nothing gets hit
			}
		}
	}

	// send muzzle flash
	gi.WriteByte(svc_muzzleflash);
	gi.WriteShort(ent - g_edicts);
	gi.WriteByte(MZ_SSHOTGUN | is_silenced);
	gi.multicast(ent->s.origin, MULTICAST_PVS);

	gi.sound(ent, CHAN_WEAPON, gi.soundindex("weapons/sshotf1b.wav"), 1, ATTN_NORM, 0);

	ent->client->ps.gunframe++;
	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (!((int)dmflags->value & DF_INFINITE_AMMO))
		ent->client->pers.inventory[ent->client->ammo_index] -= 2;
}

void Weapon_SuperShotgun (edict_t *ent)
{
	static int	pause_frames[]	= {29, 42, 57, 0};
	static int	fire_frames[]	= {7, 0};

	Weapon_Generic (ent, 6, 17, 57, 61, pause_frames, fire_frames, weapon_supershotgun_fire);
}

/*
======================================================================

RAILGUN

======================================================================
*/

void weapon_railgun_fire(edict_t *ent)
{
	vec3_t	start_left, start_right, end_left, end_right, forward, right, offset;
	int		damage, kick;
	trace_t	tr_left, tr_right;
	float	beam_offset = 8.0f;

	if (deathmatch->value)
	{
		damage = 100;
		kick = 200;
	}
	else 
	{
		damage = 450;
		kick = 350;
	}

	if (is_quad) 
	{
		damage *= 4;
		kick *= 4;
	}

	AngleVectors(ent->client->v_angle, forward, right, NULL);

	VectorScale(forward, -3, ent->client->kick_origin);
	ent->client->kick_angles[0] = -3;

	// left beam
	VectorSet(offset, 0, 7 - beam_offset, ent->viewheight - 8);
	P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start_left);
	VectorMA(start_left, 8192, forward, end_left);

	// right beam
	VectorSet(offset, 0, 7 + beam_offset, ent->viewheight - 8);
	P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start_right);
	VectorMA(start_right, 8192, forward, end_right);

	// fire both
	tr_left = gi.trace(start_left, NULL, NULL, end_left, ent, MASK_SHOT);
	tr_right = gi.trace(start_right, NULL, NULL, end_right, ent, MASK_SHOT);

	fire_rail(ent, start_left, forward, damage, kick);
	fire_rail(ent, start_right, forward, damage, kick);

	// send muzzle flash
	gi.WriteByte(svc_muzzleflash);
	gi.WriteShort(ent - g_edicts);
	gi.WriteByte(MZ_RAILGUN | is_silenced);
	gi.multicast(ent->s.origin, MULTICAST_PVS);

	// making markers so to see whats happening
	if (tr_left.fraction < 1.0) {
		edict_t* marker = G_Spawn();
		marker->classname = "railgun_marker_left";
		VectorCopy(tr_left.endpos, marker->s.origin);
		marker->movetype = MOVETYPE_NONE;
		marker->solid = SOLID_NOT;
		marker->s.effects = EF_COLOR_SHELL;
		marker->s.renderfx = RF_SHELL_RED;
		marker->think = G_FreeEdict;
		marker->nextthink = level.time + 1.0;
		gi.linkentity(marker);
	}
	if (tr_right.fraction < 1.0) {
		edict_t* marker = G_Spawn();
		marker->classname = "railgun_marker_right";
		VectorCopy(tr_right.endpos, marker->s.origin);
		marker->movetype = MOVETYPE_NONE;
		marker->solid = SOLID_NOT;
		marker->s.effects = EF_COLOR_SHELL;
		marker->s.renderfx = RF_SHELL_BLUE;
		marker->think = G_FreeEdict;
		marker->nextthink = level.time + 1.0;
		gi.linkentity(marker);
	}

	ent->client->ps.gunframe++;
	PlayerNoise(ent, ent->s.origin, PNOISE_WEAPON);

	if (!((int)dmflags->value & DF_INFINITE_AMMO))
		ent->client->pers.inventory[ent->client->ammo_index]--;
}

void Weapon_Railgun (edict_t *ent)
{
	static int	pause_frames[]	= {56, 0};
	static int	fire_frames[]	= {4, 0};

	Weapon_Generic (ent, 3, 18, 56, 61, pause_frames, fire_frames, weapon_railgun_fire);
}

/*
======================================================================

BFG10K

======================================================================
*/

void weapon_bfg_fire(edict_t *ent)
{
	vec3_t	offset, start, forward, right, left_dir, right_dir;
	int		damage;
	float	damage_radius = 3000;
	float	spread_angle = 10.0f; 

	if (deathmatch->value)
		damage = 200;
	else
		damage = 520;

	if (ent->client->ps.gunframe == 9)
	{
		// send muzzle flash
		gi.WriteByte(svc_muzzleflash);
		gi.WriteShort(ent - g_edicts);
		gi.WriteByte(MZ_BFG | is_silenced);
		gi.multicast(ent->s.origin, MULTICAST_PVS);

		ent->client->ps.gunframe++;
		PlayerNoise(ent, ent->s.origin, PNOISE_WEAPON);
		return;
	}

	// cells can go down during windup (from power armor hits), so
	// check again and abort firing if we don't have enough now
	if (ent->client->pers.inventory[ent->client->ammo_index] < 50)
	{
		ent->client->ps.gunframe++;
		return;
	}

	if (is_quad)
		damage *= 4;

	AngleVectors(ent->client->v_angle, forward, right, NULL);

	VectorScale(forward, -2, ent->client->kick_origin);

	// make a big pitch kick with an inverse fall
	ent->client->v_dmg_pitch = -20;
	ent->client->v_dmg_roll = crandom() * 10;
	ent->client->v_dmg_time = level.time + DAMAGE_TIME;

	VectorSet(offset, 8, 8, ent->viewheight - 8);
	P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start);

	// center ball
	fire_bfg(ent, start, forward, damage, 200, damage_radius);

	// left ball
	VectorCopy(forward, left_dir);
	// rotating left_dir by -spread_angle degrees around the up axis, doing new calc ways to make this interesting
	left_dir[0] = forward[0] * cos(-spread_angle * M_PI / 180.0f) - forward[1] * sin(-spread_angle * M_PI / 180.0f);
	left_dir[1] = forward[0] * sin(-spread_angle * M_PI / 180.0f) + forward[1] * cos(-spread_angle * M_PI / 180.0f);
	VectorNormalize(left_dir);
	fire_bfg(ent, start, left_dir, damage, 200, damage_radius);

	// right ball
	VectorCopy(forward, right_dir);
	// doing the same thing of rotating right_dir by +spread_angle degrees around the up axis
	right_dir[0] = forward[0] * cos(spread_angle * M_PI / 180.0f) - forward[1] * sin(spread_angle * M_PI / 180.0f);
	right_dir[1] = forward[0] * sin(spread_angle * M_PI / 180.0f) + forward[1] * cos(spread_angle * M_PI / 180.0f);
	VectorNormalize(right_dir);
	fire_bfg(ent, start, right_dir, damage, 200, damage_radius);

	// colored markers for visual, if it works
	for (int i = 0; i < 3; ++i) {
		edict_t* marker = G_Spawn();
		marker->classname = "bfg_marker";
		VectorCopy(start, marker->s.origin);
		marker->movetype = MOVETYPE_NONE;
		marker->solid = SOLID_NOT;
		marker->s.effects = EF_COLOR_SHELL;
		if (i == 0)
			marker->s.renderfx = RF_SHELL_GREEN; // center
		else if (i == 1)
			marker->s.renderfx = RF_SHELL_RED;   // left
		else
			marker->s.renderfx = RF_SHELL_BLUE;  // right
		marker->think = G_FreeEdict;
		marker->nextthink = level.time + 1.0;
		gi.linkentity(marker);
	}

	ent->client->ps.gunframe++;
	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (!((int)dmflags->value & DF_INFINITE_AMMO))
		ent->client->pers.inventory[ent->client->ammo_index] -= 50;
}

void Weapon_BFG (edict_t *ent)
{
	static int	pause_frames[]	= {39, 45, 50, 55, 0};
	static int	fire_frames[]	= {9, 17, 0};

	Weapon_Generic (ent, 8, 32, 55, 58, pause_frames, fire_frames, weapon_bfg_fire);
}


//======================================================================
