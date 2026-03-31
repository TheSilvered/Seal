#ifndef SL_VARTABLE_H_
#define SL_VARTABLE_H_

#include "sl_hashmap.h"
#include "sl_vm.h"

typedef struct SlVarTable {
    struct SlVarTable *parent;
    SlStrMap vars;
} SlVarTable;

// Create a new var table.
SlVarTable *slNewVarTable(SlVM *vm, uint8_t *strs, SlVarTable *parent);
// Destroy `table` and return its parent.
SlVarTable *slDelVarTable(SlVarTable *table);

bool slVarTableSet(SlVM *vm, SlVarTable *table, SlStrIdx name, uint32_t value);
uint32_t *slVarTableGet(SlVarTable *table, SlStrIdx name);

#endif // !SL_VARTABLE_H_
