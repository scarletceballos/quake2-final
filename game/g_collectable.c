#include "g_collectable.h"
#include <string.h>
#include "g_local.h"

void Collectable_Init(Collectable* collectable, CollectableType type, int quantity) {
    if (collectable != NULL) {
        collectable->type = type;
        collectable->quantity = quantity;
    }
}

CollectableType Collectable_GetType(const Collectable* collectable) {
    return (collectable != NULL) ? collectable->type : COLLECTABLE_WOOD;
}

int Collectable_GetQuantity(const Collectable* collectable) {
    return (collectable != NULL) ? collectable->quantity : 0;
}

void Collectable_AddQuantity(Collectable* collectable, int amount) {
    if (collectable != NULL) {
        collectable->quantity += amount;
    }
}

void Collectable_RemoveQuantity(Collectable* collectable, int amount) {
    if (collectable != NULL && collectable->quantity >= amount) {
        collectable->quantity -= amount;
    }
}

const char* Collectable_GetTypeName(CollectableType type) {
    switch (type) {
    case COLLECTABLE_WOOD: return "Wood";
    case COLLECTABLE_METAL: return "Metal";
    case COLLECTABLE_STONE: return "Stone";
    case COLLECTABLE_ORE: return "Ore";
    case COLLECTABLE_MECHANICAL_PARTS: return "Mechanical Parts";
    default: return "Unknown";
    }
}
weapon_cost_t weapon_costs[] = {
    // weapon_name,              wood, metal, stone, ore, mech_parts
    {"Tactical Pistol",           10,   5,     0,    0,   0},
    {"Pump Shotgun",              20,   10,    5,    0,   0},
    {"Tactical Shotgun",          30,   15,    10,   0,   0},
    {"Assault Rifle",             40,   20,    15,   5,   0},
    {"Burst Assault Rifle",       50,   25,    20,   10,  5},
    {"Heavy Sniper",              60,   30,    25,   15,  10},
    {"Grenade Launcher",          70,   35,    30,   20,  15},
    {"Rocket Launcher",           80,   40,    35,   25,  20},
    {"BFG10K",                   100,   50,    40,   30,  25},
    {NULL, 0, 0, 0, 0, 0}  // Terminator
};

/*
==================
PurchaseWeapon
Allows player to buy a weapon using collectables
==================
*/
qboolean PurchaseWeapon(edict_t* ent, char* weapon_name)
{
    int i;
    gitem_t* item;

    // Find the weapon in the costs table
    for (i = 0; weapon_costs[i].weapon_name != NULL; i++) {
        if (Q_stricmp(weapon_costs[i].weapon_name, weapon_name) == 0) {
            break;
        }
    }

    // Weapon not found just in case
    if (weapon_costs[i].weapon_name == NULL) {
        gi.cprintf(ent, PRINT_HIGH, "Unknown weapon: %s\n", weapon_name);
        return false;
    }

    // Check if player has enough collectables
    if (ent->client->pers.inventory[COLLECTABLE_WOOD] < weapon_costs[i].wood_cost ||
        ent->client->pers.inventory[COLLECTABLE_METAL] < weapon_costs[i].metal_cost ||
        ent->client->pers.inventory[COLLECTABLE_STONE] < weapon_costs[i].stone_cost ||
        ent->client->pers.inventory[COLLECTABLE_ORE] < weapon_costs[i].ore_cost ||
        ent->client->pers.inventory[COLLECTABLE_MECHANICAL_PARTS] < weapon_costs[i].mech_parts_cost) {

        gi.cprintf(ent, PRINT_HIGH, "Not enough resources to purchase %s\n", weapon_name);
        return false;
    }

    // Find weapon
    item = FindItem(weapon_name);
    if (!item) {
        gi.cprintf(ent, PRINT_HIGH, "Unknown weapon: %s\n", weapon_name);
        return false;
    }

    // Check if player already has weapon
    if (ent->client->pers.inventory[ITEM_INDEX(item)]) {
        gi.cprintf(ent, PRINT_HIGH, "You already have the %s\n", weapon_name);
        return false;
    }

    // Deduct collectables
    ent->client->pers.inventory[COLLECTABLE_WOOD] -= weapon_costs[i].wood_cost;
    ent->client->pers.inventory[COLLECTABLE_METAL] -= weapon_costs[i].metal_cost;
    ent->client->pers.inventory[COLLECTABLE_STONE] -= weapon_costs[i].stone_cost;
    ent->client->pers.inventory[COLLECTABLE_ORE] -= weapon_costs[i].ore_cost;
    ent->client->pers.inventory[COLLECTABLE_MECHANICAL_PARTS] -= weapon_costs[i].mech_parts_cost;

    // Give weapon to player
    ent->client->pers.inventory[ITEM_INDEX(item)] = 1;

    gi.cprintf(ent, PRINT_HIGH, "Weapon purchased: %s\n", weapon_name);
    gi.cprintf(ent, PRINT_HIGH, "Updated inventory - Wood: %d, Metal: %d, Stone: %d, Ore: %d, Mech Parts: %d\n",
        ent->client->pers.inventory[COLLECTABLE_WOOD],
        ent->client->pers.inventory[COLLECTABLE_METAL],
        ent->client->pers.inventory[COLLECTABLE_STONE],
        ent->client->pers.inventory[COLLECTABLE_ORE],
        ent->client->pers.inventory[COLLECTABLE_MECHANICAL_PARTS]);

    if (item->flags & IT_WEAPON) {
        ent->client->newweapon = item;
    }

    // Give some default ammo for the weapon
    if (item->ammo) {
        gitem_t* ammo_item = FindItem(item->ammo);
        if (ammo_item) {
            Add_Ammo(ent, ammo_item, ammo_item->quantity * 2); // Double normal pickup amount
        }
    }

    gi.sound(ent, CHAN_ITEM, gi.soundindex("misc/tele_up.wav"), 1, ATTN_NORM, 0);
    gi.cprintf(ent, PRINT_HIGH, "You purchased: %s\n", weapon_name);

    return true;
}