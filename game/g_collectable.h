#ifndef COLLECTABLE_H
#define COLLECTABLE_H

#ifdef __cplusplus
extern "C" {
#endif

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

    // Function declarations
    void Collectable_Init(Collectable* collectable, CollectableType type, int quantity);
    CollectableType Collectable_GetType(const Collectable* collectable);
    int Collectable_GetQuantity(const Collectable* collectable);
    void Collectable_AddQuantity(Collectable* collectable, int amount);
    void Collectable_RemoveQuantity(Collectable* collectable, int amount);
    const char* Collectable_GetTypeName(CollectableType type);

#ifdef __cplusplus
}
#endif

#endif // COLLECTABLE_H
