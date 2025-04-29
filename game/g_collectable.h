#ifndef COLLECTABLE_H
#define COLLECTABLE_H

struct edict_s;
typedef struct edict_s edict_t;

typedef int qboolean;

#include <stddef.h>

// Enum for collectable types
typedef enum {
    COLLECTABLE_WOOD,
    COLLECTABLE_METAL,
    COLLECTABLE_STONE,
    COLLECTABLE_ORE,
    COLLECTABLE_MECHANICAL_PARTS
} CollectableType;

// Struct for a collectable
typedef struct {
    CollectableType type;
    int quantity;
} Collectable;

typedef struct {
    char* weapon_name;       
    int wood_cost;           
    int metal_cost;          
    int stone_cost;          
    int ore_cost;          
    int mech_parts_cost;
} weapon_cost_t;

extern weapon_cost_t weapon_costs[];

// Function declarations
void Collectable_Init(Collectable* collectable, CollectableType type, int quantity);
CollectableType Collectable_GetType(const Collectable* collectable);
int Collectable_GetQuantity(const Collectable* collectable);
void Collectable_AddQuantity(Collectable* collectable, int amount);
void Collectable_RemoveQuantity(Collectable* collectable, int amount);
const char* Collectable_GetTypeName(CollectableType type);
void Cmd_Shop_f(edict_t* ent);

qboolean PurchaseWeapon(edict_t* ent, char* weapon_name);

#endif // COLLECTABLE_H
