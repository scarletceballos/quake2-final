#include "g_collectable.h"
#include <string.h>

void Collectabke_Init(Collectable* collectable, CollectableType type, int quantity) {
	if (collectable != NULL) {
		collectable->type = type;
		collectable->quantity = quantity;
	}
}

CollectableType Collectable_GetType(const Collectable* collectable) {
	return collectable != NULL ? collectable->type : COLLECTABLE_WOOD;
}

int Collectable_GetQuantity(const Collectable* collectable) {
	return collectable != NULL ? collectable->quantity : 0;
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
	default: return "Unknown (error)";
	}
}