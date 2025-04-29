#include "g_local.h"   
#include "g_collectable.h"

/*
==================
CollectablesLayout
Returns layout string for collectables menu
==================
*/
void CollectablesLayout(edict_t* ent)
{
    char string[1024];

    // Layout String here
    Com_sprintf(string, sizeof(string),
        "xv 10 yv 20 string2 \"COLLECTABLES\" "
        "xv 10 yv 35 string \"Wood: %i\" "
        "xv 10 yv 45 string \"Metal: %i\" "
        "xv 10 yv 55 string \"Stone: %i\" "
        "xv 10 yv 65 string \"Ore: %i\" "
        "xv 10 yv 75 string \"Mech Parts: %i\" ",
        ent->client->pers.inventory[COLLECTABLE_WOOD],
        ent->client->pers.inventory[COLLECTABLE_METAL],
        ent->client->pers.inventory[COLLECTABLE_STONE],
        ent->client->pers.inventory[COLLECTABLE_ORE],
        ent->client->pers.inventory[COLLECTABLE_MECHANICAL_PARTS]
    );

    gi.WriteByte(svc_layout);
    gi.WriteString(string);
    gi.unicast(ent, true);
}

/*
==================
Cmd_ShowCollectables_f
Toggle collectables display
==================
*/
void Cmd_ShowCollectables_f(edict_t* ent)
{
    // Toggle the display
    if (ent->client->ps.stats[STAT_SHOWCOLLECTABLES])
        ent->client->ps.stats[STAT_SHOWCOLLECTABLES] = 0;
    else
        ent->client->ps.stats[STAT_SHOWCOLLECTABLES] = 1;

    if (ent->client->ps.stats[STAT_SHOWCOLLECTABLES])
        CollectablesLayout(ent);
}

/*
==============
G_SetStatusBar
==============
*/
void G_SetStatusBar(void)
{
    static char statusbar[] =

        "yb -24 "
        "xv 0 "
        "hnum "
        "xv 50 "
        "pic 0 "
        "xv 10 "
        "yv 10 "
        "string2 \"Resources: \" "

        // Show collectables as "W:# M:# S:# O:# P:#" on one line text would probably be too long
        "xv 10 "
        "yv 20 "
        "string \"W:\" "
        "xv 25 "
        "yv 20 "
        "num 2 31 " // Wood

        "xv 40 "
        "yv 20 "
        "string \"M:\" "
        "xv 55 "
        "yv 20 "
        "num 2 32 " // Metal

        "xv 70 "
        "yv 20 "
        "string \"S:\" "
        "xv 85 "
        "yv 20 "
        "num 2 33 " // Stone

        "xv 100 "
        "yv 20 "
        "string \"O:\" "
        "xv 115 "
        "yv 20 "
        "num 2 34 " // Ore

        "xv 130 "
        "yv 20 "
        "string \"P:\" "
        "xv 145 "
        "yv 20 "
        "num 2 35 "; // mECH Parts

    gi.configstring(CS_STATUSBAR, statusbar);
}

/*
==================
ShopLayout
Display weapon shop UI with weapon costs and player's current resources
==================
*/
void ShopLayout(edict_t* ent)
{

    if (!ent || !ent->client) {
        gi.cprintf(NULL, PRINT_HIGH, "Invalid entity or client.\n");
        return;
    }

    // Reduce buffer sizes to prevent network overflow cause it happend too often
    char string[1400];       // Reduced from 2048 MAX
    char title[64];          // Reduced from 128 MAX
    char resourcesLine[128]; // Reduced from 256 MAX
    char weaponsText[800] = ""; // Reduced from 1024 MAX
    int weaponCount = 0;
    int i;
    int baseY = 30;
    int lineHeight = 10;

    Com_sprintf(title, sizeof(title),
        "xv 80 yv 10 string2 \"==== WEAPON SHOP ====\" ");

    Com_sprintf(resourcesLine, sizeof(resourcesLine),
        "xv 20 yv %d string \"W:%d M:%d S:%d O:%d P:%d\" ",
        baseY,
        ent->client->pers.inventory[COLLECTABLE_WOOD],
        ent->client->pers.inventory[COLLECTABLE_METAL],
        ent->client->pers.inventory[COLLECTABLE_STONE],
        ent->client->pers.inventory[COLLECTABLE_ORE],
        ent->client->pers.inventory[COLLECTABLE_MECHANICAL_PARTS]);

    char headerLine[128]; // Reduced from 256 - want no overflow
    Com_sprintf(headerLine, sizeof(headerLine),
        "xv 20 yv %d string2 \"Weapon        Cost (W/M/S/O/P)\" ",
        baseY + lineHeight * 2);

    typedef struct {
        char* weapon_name;         // FindItem Name
        char* display_name; 
        int wood_cost;
        int metal_cost;
        int stone_cost;
        int ore_cost;
        int mech_parts_cost;
    } local_weapon_cost_t;

    local_weapon_cost_t local_weapon_costs[] = {
        {"weapon_shotgun", "Pump Shotgun", 1, 1, 1, 0, 0},
        {"weapon_supershotgun", "Tactical Shotgun", 2, 2, 2, 0, 0},
        {"weapon_machinegun", "Assault Rifle", 3, 3, 3, 1, 0},
        {"weapon_chaingun", "Burst Assault Rifle", 4, 4, 4, 2, 1},
        {"weapon_railgun", "Heavy Sniper", 5, 5, 5, 5, 2},
        {"weapon_grenadelauncher", "Grenade Launcher", 6, 6, 6, 6, 3},
        {"weapon_rocketlauncher", "Avil Rocket Launcher", 7, 7, 7, 7, 4},
        {"weapon_bfg", "BFG10K", 8, 8, 8, 8, 5},
        {NULL, NULL, 0, 0, 0, 0, 0} // Terminator
    };


    // Build the weapon list with costs
    for (i = 0; local_weapon_costs[i].weapon_name != NULL; i++) {
        char weaponLine[128]; // Reduced from 256 cause of overflow
        char status[16] = ""; // Reduced from 32 cause of overflow

        // Check if player can afford this weapon
        if (ent->client->pers.inventory[COLLECTABLE_WOOD] >= local_weapon_costs[i].wood_cost &&
            ent->client->pers.inventory[COLLECTABLE_METAL] >= local_weapon_costs[i].metal_cost &&
            ent->client->pers.inventory[COLLECTABLE_STONE] >= local_weapon_costs[i].stone_cost &&
            ent->client->pers.inventory[COLLECTABLE_ORE] >= local_weapon_costs[i].ore_cost &&
            ent->client->pers.inventory[COLLECTABLE_MECHANICAL_PARTS] >= local_weapon_costs[i].mech_parts_cost) {
            strncpy(status, " [Buy]", sizeof(status) - 1);
            status[sizeof(status) - 1] = '\0'; 
        }

        // Mark weapons the player already has
        gitem_t* item = FindItem(local_weapon_costs[i].weapon_name);
        if (item && ent->client->pers.inventory[ITEM_INDEX(item)]) {
            strncpy(status, " [Own]", sizeof(status) - 1);
            status[sizeof(status) - 1] = '\0'; 
        }

        Com_sprintf(weaponLine, sizeof(weaponLine),
            "xv 20 yv %d string \"%d) %-11s %d/%d/%d/%d/%d%s\" ",
            baseY + lineHeight * (3 + i),
            i + 1,
            local_weapon_costs[i].display_name, 
            local_weapon_costs[i].wood_cost,
            local_weapon_costs[i].metal_cost,
            local_weapon_costs[i].stone_cost,
            local_weapon_costs[i].ore_cost,
            local_weapon_costs[i].mech_parts_cost,
            status);

        if (strlen(weaponsText) + strlen(weaponLine) < sizeof(weaponsText)) {
            strcat(weaponsText, weaponLine);
            weaponCount++;
        }
        else {
            gi.cprintf(ent, PRINT_HIGH, "Too many weapons to display in shop (should never happen) \n");
            break;
        }
    }
    char instructLine[128]; // Reduced from 256
    Com_sprintf(instructLine, sizeof(instructLine),
        "xv 20 yv %d string2 \"Press 1-%d to buy, F to close\" ",
        baseY + lineHeight * (4 + weaponCount),
        weaponCount);

    // Debug information
    int totalLength = strlen(title) + strlen(resourcesLine) + strlen(headerLine) +
        strlen(weaponsText) + strlen(instructLine);
    //gi.dprintf("Shop layout length: %d bytes\n", totalLength);

    // Combine all elements into the final layout
    // Check for potential buffer overflow
    if (totalLength < sizeof(string) - 10) { // Leave some margin for safety
        Com_sprintf(string, sizeof(string),
            "%s"   // Title
            "%s"   // Resources display
            "%s"   // Column headers
            "%s"   // Weapon listings
            "%s",  // Instructions
            title, resourcesLine, headerLine, weaponsText, instructLine);

        gi.WriteByte(svc_layout);
        gi.WriteString(string);
        gi.unicast(ent, true);
    }
    else {
        gi.cprintf(ent, PRINT_HIGH, "Shop layout too large to display (%d bytes).\n", totalLength);
    }
}


/*
==================
Cmd_Shop_f
Toggle the shop display
==================
*/
void Cmd_Shop_f(edict_t* ent)
{
    // Toggle display state
    if (ent->client->showshop) {
        // Close the shop menu
        ent->client->showshop = false;
        return;
    }

    // Clear other flags
    ent->client->showscores = false;
    ent->client->showinventory = false;
    ent->client->showhelp = false;

    // Show shop layout
    ent->client->showshop = true;
    ShopLayout(ent);

    return;
}

/*
==================
ProcessShopSelection
Handle numeric key presses in the shop
==================
*/
void ProcessShopSelection(edict_t* ent, int impulse)
{
    if (!ent || !ent->client) {
        gi.cprintf(NULL, PRINT_HIGH, "Invalid entity or client.\n");
        return;
    }
    int num_weapons = 0;
    while (weapon_costs[num_weapons].weapon_name != NULL) {
        num_weapons++;
    }

    if (impulse < 1 || impulse > num_weapons) {
        gi.cprintf(ent, PRINT_HIGH, "Invalid selection.\n");
        return;
    }

    weapon_cost_t selected_weapon = weapon_costs[impulse - 1];
    gitem_t* item = FindItem(selected_weapon.weapon_name);

    if (!item) {
        gi.cprintf(ent, PRINT_HIGH, "Weapon not found: %s\n", selected_weapon.weapon_name);
        return;
    }
}