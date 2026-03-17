#ifndef SL_VARTABLE_H_
#define SL_VARTABLE_H_

#include "sl_hashmap.h"

typedef struct SlVarTable {
    struct SlVarTable *parent;
    SlStrMap vars;
} SlVarTable;

// Create a new var table.
SlVarTable *slNewVarTable(uint8_t *strs, SlVarTable *parent);
// Destroy `table` and return its parent.
SlVarTable *slDelVarTable(SlVarTable *table);

bool slVarTableSet(SlVarTable *table, SlStrIdx name, uint32_t value);
uint32_t *slVarTableGet(SlVarTable *table, SlStrIdx name);

#endif // !SL_VARTABLE_H_
