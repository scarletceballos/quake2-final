/*
===============
Wave-based Enemy Spawning
===============
*/

#include "g_local.h"

wave_system_t g_wave_system;

/*
===============
SpawnEnemy
Spawns a single enemy of the specified type at the specified location
===============
*/
/*
===============
SpawnEnemy
Spawns a single enemy of the specified type at the specified location
With enhanced initialization to bypass map removal checks
===============
*/
void SpawnEnemy(char* classname, vec3_t origin, vec3_t angles)
{
    edict_t* enemy;

    // Create a new entity
    enemy = G_Spawn();

    // Set up basic entity properties
    VectorCopy(origin, enemy->s.origin);
    VectorCopy(angles, enemy->s.angles);
    enemy->classname = classname;

    // Force the entity to be usable by overriding any map-based settings
    enemy->spawnflags = 0;         // Clear any spawn flags
    enemy->svflags &= ~SVF_NOCLIENT; // Make sure entity is visible
    enemy->solid = SOLID_BBOX;     // Make entity solid
    enemy->movetype = MOVETYPE_STEP; // Standard monster movement
    enemy->takedamage = DAMAGE_AIM; // Can be damaged

    // Spawn the appropriate monster based on classname
    if (strcmp(classname, "monster_soldier") == 0)
    {
        SP_monster_soldier(enemy);
    }
    else if (strcmp(classname, "monster_soldier_light") == 0)
    {
        SP_monster_soldier_light(enemy);
    }
    else if (strcmp(classname, "monster_soldier_ss") == 0)
    {
        SP_monster_soldier_ss(enemy);
    }
    else if (strcmp(classname, "monster_infantry") == 0)
    {
        SP_monster_infantry(enemy);
    }
    else if (strcmp(classname, "monster_gunner") == 0)
    {
        SP_monster_gunner(enemy);
    }
    else if (strcmp(classname, "monster_mutant") == 0)
    {
        SP_monster_mutant(enemy);
    }
    else
    {
        // Default to a soldier if unrecognized enemy type
        SP_monster_soldier(enemy);
    }

    // Additional setup to ensure monster functions correctly
    if (enemy->think) {
        enemy->nextthink = level.time + FRAMETIME;
    }

    // Force the monster to initialize its AI properly
    if (enemy->monsterinfo.stand) {
        enemy->monsterinfo.stand(enemy);
    }

    // Make sure the monster is actively pursuing the player
    edict_t* player = &g_edicts[1]; // First player
    if (player && player->inuse && player->health > 0) {
        // Set the monster's enemy to be the player
        enemy->enemy = player;
        FoundTarget(enemy);
    }

    // Update counts
    g_wave_system.en_spawned++;
    g_wave_system.en_remaining--;

    gi.dprintf("Spawned enemy of type: %s at (%f, %f, %f)\n",
        classname, origin[0], origin[1], origin[2]);
}

/*
===============
GetRandSpwnLocation
Find a spawn point for enemies near the player
===============
*/
/*
===============
GetRandSpwnLocation
Find a spawn point for enemies near the player - with improved reliability
===============
*/
qboolean GetRandSpwnLocation(vec3_t origin, vec3_t angles)
{
    edict_t* player = NULL;
    vec3_t spawn_offset;
    trace_t trace;
    vec3_t mins = { -16, -16, -24 };  // Standard monster size
    vec3_t maxs = { 16, 16, 32 };
    int max_attempts = 20;  // Increased attempts for better reliability

    // Find an active player to spawn near
    for (int i = 1; i <= maxclients->value; i++) {
        player = &g_edicts[i];
        if (player && player->inuse && player->client && player->health > 0) {
            break;
        }
        player = NULL;
    }

    if (!player) {
        gi.dprintf("WARNING: No valid player found for enemy spawning.\n");
        return false;
    }

    // Store player view direction for later use
    vec3_t player_forward;
    AngleVectors(player->client->v_angle, player_forward, NULL, NULL);

    // First attempt: Try spawning in a radius around the player
    for (int attempt = 0; attempt < max_attempts; attempt++) {
        float radius = 300.0 + (rand() % 300);  // 300-600 units away
        float angle = (float)(rand() % 360) * (M_PI / 180.0);  // Random angle

        // For even-numbered attempts, try to spawn in front of player
        // where they're more likely to see the action
        if (attempt % 2 == 0) {
            // Adjust angle to be within 120 degrees of player's forward view
            float base_angle = atan2f(player_forward[1], player_forward[0]);
            angle = base_angle + ((rand() % 240 - 120) * (M_PI / 180.0));
        }

        // Calculate offset from player
        spawn_offset[0] = cosf(angle) * radius;
        spawn_offset[1] = sinf(angle) * radius;
        spawn_offset[2] = 0;

        // Position = player position + offset
        VectorAdd(player->s.origin, spawn_offset, origin);

        // First trace: Check if this position is valid in the world
        trace = gi.trace(player->s.origin, NULL, NULL, origin, player, MASK_SOLID);

        // If trace hit something, adjust our spawn to that point (minus a safety margin)
        if (trace.fraction < 1.0) {
            vec3_t dir;
            VectorSubtract(trace.endpos, player->s.origin, dir);
            VectorNormalize(dir);
            // Pull back by 32 units to avoid spawning in a wall
            VectorMA(player->s.origin, trace.fraction * radius - 32, dir, origin);
        }

        // Second trace: Find the floor at this position
        vec3_t floor_check_start, floor_check_end;
        VectorCopy(origin, floor_check_start);
        VectorCopy(origin, floor_check_end);
        floor_check_start[2] += 64;  // Start above the potential spawn point
        floor_check_end[2] -= 256;   // Trace down further to find floor

        trace = gi.trace(floor_check_start, NULL, NULL, floor_check_end, NULL, MASK_SOLID);

        // If we hit floor, place the monster there
        if (trace.fraction < 1.0) {
            // Set the origin to the hit point, slightly above the surface
            VectorCopy(trace.endpos, origin);
            origin[2] += 1;  // Lift slightly off the floor

            // Check if there's enough room for the monster
            trace = gi.trace(origin, mins, maxs, origin, NULL, MASK_MONSTERSOLID);

            if (!trace.startsolid && !trace.allsolid) {
                // Set angles - face toward player
                vec3_t dir;
                VectorSubtract(player->s.origin, origin, dir);
                vectoangles(dir, angles);
                angles[PITCH] = 0;  // Keep monster upright

                gi.dprintf("Found valid spawn at (%f, %f, %f), attempt: %d\n",
                    origin[0], origin[1], origin[2], attempt + 1);
                return true;
            }
        }
    }

    // Last resort: Direct spawn near player
    // Find a nearby safe position where monsters can function
    vec3_t direct_spawn_positions[4];
    int valid_positions = 0;

    // Try 4 positions around the player at cardinal directions
    for (int i = 0; i < 4; i++) {
        float angle = i * (M_PI / 2.0);  // 0, 90, 180, 270 degrees
        VectorCopy(player->s.origin, direct_spawn_positions[i]);
        direct_spawn_positions[i][0] += cosf(angle) * 96;  // Closer, but not too close
        direct_spawn_positions[i][1] += sinf(angle) * 96;

        // Check if this position is valid
        trace = gi.trace(direct_spawn_positions[i], mins, maxs,
            direct_spawn_positions[i], NULL, MASK_MONSTERSOLID);
        if (!trace.startsolid && !trace.allsolid) {
            valid_positions++;
        }
    }

    // If any positions are valid, choose one
    if (valid_positions > 0) {
        int selected_position;
        do {
            selected_position = rand() % 4;
            trace = gi.trace(direct_spawn_positions[selected_position], mins, maxs,
                direct_spawn_positions[selected_position], NULL, MASK_MONSTERSOLID);
        } while (trace.startsolid || trace.allsolid);

        VectorCopy(direct_spawn_positions[selected_position], origin);

        // Face toward player
        vec3_t dir;
        VectorSubtract(player->s.origin, origin, dir);
        vectoangles(dir, angles);
        angles[PITCH] = 0;

        gi.dprintf("Using fallback cardinal direction spawn near player\n");
        return true;
    }

    // ABSOLUTE last resort: Spawn above the player
    VectorCopy(player->s.origin, origin);
    origin[2] += 64;  // Above player's head but not too far
    VectorCopy(player->s.angles, angles);
    angles[PITCH] = 0;

    gi.dprintf("EMERGENCY: Spawning directly above player\n");
    return true;
}

/*
===============
WaveThink
Main think function for the wave system, spawns enemies over time
===============
*/
/*
===============
WaveThink
Main think function for the wave system, spawns enemies over time
===============
*/
void WaveThink(void)
{
    static vec3_t origin, angles;
    static float last_debug_time = 0;
    static int spawn_failure_count = 0;

    // If no active wave, nothing to do
    if (!g_wave_system.active_wave)
        return;

    // Debug output at intervals (not every frame)
    if (level.time > last_debug_time + 5.0) {
        last_debug_time = level.time;
        gi.dprintf("Wave status: Active=%d, Remaining=%d, Spawned=%d, NextSpawn=%.1f\n",
            g_wave_system.active_wave, g_wave_system.en_remaining,
            g_wave_system.en_spawned, g_wave_system.next_spawn_time - level.time);
    }

    // If all enemies in the wave are spawned, check if the wave is complete
    if (g_wave_system.en_remaining <= 0)
    {
        // Count existing monsters to see if wave is really complete
        int count = 0;
        edict_t* ent;
        for (ent = g_edicts; ent < &g_edicts[globals.num_edicts]; ent++)
        {
            if (!ent->inuse || ent->client || ent->svflags & SVF_NOCLIENT)
                continue;

            // Count any active enemy
            if ((ent->monsterinfo.aiflags & AI_GOOD_GUY) == 0 &&
                ent->health > 0 &&
                ent->svflags & SVF_MONSTER)
            {
                count++;
            }
        }

        // If no monsters remain, the wave is complete
        if (count == 0)
        {
            g_wave_system.active_wave = false;
            gi.bprintf(PRINT_HIGH, "Wave %d complete!\n", g_wave_system.curr_wave);
            spawn_failure_count = 0; // Reset failure counter
            return;
        }
    }

    // Time to spawn a new enemy?
    if (level.time >= g_wave_system.next_spawn_time && g_wave_system.en_remaining > 0)
    {
        // Find a spawn location near the player
        if (GetRandSpwnLocation(origin, angles))
        {
            // Define a list of reliable enemy types
            const char* enemy_types[] = {
                "monster_soldier",
                "monster_soldier_light",
                "monster_infantry"
            };

            // Select an enemy type appropriate for current wave
            int enemy_index = rand() % (sizeof(enemy_types) / sizeof(enemy_types[0]));
            const char* enemy_type = enemy_types[enemy_index];

            // Spawn the enemy
            SpawnEnemy((char*)enemy_type, origin, angles);
            gi.dprintf("Successfully spawned %s (%d of %d)\n",
                enemy_type, g_wave_system.en_spawned, g_wave_system.en_per_wave);

            // Set next spawn time
            g_wave_system.next_spawn_time = level.time + g_wave_system.delay_spawn;

            // Reset failure counter on success
            spawn_failure_count = 0;
        }
        else {
            // If spawn failed, retry soon but avoid infinite loop with failure count
            spawn_failure_count++;

            if (spawn_failure_count >= 10) {
                // Too many failures, skip this enemy
                gi.dprintf("Failed to spawn enemy after multiple attempts, skipping.\n");
                g_wave_system.en_remaining--;
                spawn_failure_count = 0;
                g_wave_system.next_spawn_time = level.time + g_wave_system.delay_spawn;
            }
            else {
                // Try again soon
                g_wave_system.next_spawn_time = level.time + 0.2;
                gi.dprintf("Failed to find spawn location, will try again soon (attempt %d)\n",
                    spawn_failure_count);
            }
        }
    }
}

/*
===============
StartWave
Start a new enemy wave
===============
*/
/*
===============
StartWave
Start a new enemy wave with scaling difficulty
===============
*/
void StartWave(int wave_number) {
    if (g_wave_system.active_wave) {
        gi.dprintf("Wave already in progress.\n");
        return;
    }

    // Reset wave completion status every 3 waves
    if (g_wave_system.waves_completed >= g_wave_system.waves_in_set) {
        gi.bprintf(PRINT_HIGH, "Starting a new wave set!\n");
        g_wave_system.waves_completed = 0;
    }

    // Start the next wave
    g_wave_system.curr_wave = wave_number;
    g_wave_system.en_spawned = 0;

    // Scale number of enemies based on wave number
    // More enemies in higher waves, but keep reasonable
    int enemies_for_wave = 8 + (wave_number / 3);
    if (enemies_for_wave > 25) enemies_for_wave = 25; // Cap at 25

    g_wave_system.en_per_wave = enemies_for_wave;
    g_wave_system.en_remaining = g_wave_system.en_per_wave;
    g_wave_system.active_wave = true;
    g_wave_system.next_spawn_time = level.time + 0.5; // Start spawning quickly
    g_wave_system.waves_completed++;

    // Speed up spawns in higher waves, but maintain a minimum delay
    g_wave_system.delay_spawn = 2.0 - (wave_number * 0.1);
    if (g_wave_system.delay_spawn < 0.5) g_wave_system.delay_spawn = 0.5;

    gi.bprintf(PRINT_HIGH, "Starting wave %d! Prepare for %d enemies...\n",
        wave_number, g_wave_system.en_per_wave);
}


/*
===============
InitWaveSystem
Initialize the wave spawning system
===============
*/
/*
===============
InitWaveSystem
Initialize the wave spawning system
===============
*/
void InitWaveSystem(void) {
    // Initialize all values to avoid undefined behavior
    memset(&g_wave_system, 0, sizeof(g_wave_system));

    g_wave_system.curr_wave = 0;
    g_wave_system.en_per_wave = 10;
    g_wave_system.en_spawned = 0;
    g_wave_system.en_remaining = 0;
    g_wave_system.active_wave = false;
    g_wave_system.next_spawn_time = 0;
    g_wave_system.delay_spawn = 2.0;
    g_wave_system.waves_in_set = 3;
    g_wave_system.waves_completed = 0;

    gi.dprintf("Wave system initialized successfully.\n");

    if (!deathmatch->value && !coop->value) {
        gi.bprintf(PRINT_HIGH, "Wave spawning can begin. Press 'o' key to start a wave.\n");
    }
}


/*
===============
Cmd_SpawnWave_f
Command handler for spawning a wave of enemies
===============
*/
void Cmd_SpawnWave_f(edict_t* ent)
{
    // Safety check for null entity
    if (!ent || !ent->client) {
        gi.dprintf("Invalid entity tried to spawn a wave.\n");
        return;
    }

    int wave;

    // Allow any player to start a wave in single player or coop
    // In deathmatch, require sv_cheats
    if (deathmatch->value && !sv_cheats->value) {
        gi.cprintf(ent, PRINT_HIGH, "Waves can only be started in single player, coop, or with cheats enabled.\n");
        return;
    }

    // No wave commands during intermission
    if (level.intermissiontime) {
        gi.cprintf(ent, PRINT_HIGH, "Cannot start waves during intermission.\n");
        return;
    }

    if (gi.argc() < 2) {
        // If no wave specified, start the next wave
        wave = g_wave_system.curr_wave + 1;
    }
    else {
        wave = atoi(gi.argv(1));
        if (wave <= 0) {
            gi.cprintf(ent, PRINT_HIGH, "Wave number must be positive.\n");
            return;
        }
    }

    if (g_wave_system.active_wave) {
        gi.cprintf(ent, PRINT_HIGH, "A wave is already in progress.\n");
        return;
    }

    // Start the wave - this is a separate function call
    StartWave(wave);

    // Let everyone know who started the wave
    gi.bprintf(PRINT_HIGH, "%s started wave %d!\n", ent->client->pers.netname, wave);
    gi.dprintf("Cmd_SpawnWave_f called. Current wave: %d, Active: %d\n",
        g_wave_system.curr_wave, g_wave_system.active_wave);
}
