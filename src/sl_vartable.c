#include "sl_vartable.h"

SlVarTable *slNewVarTable(SlVM *vm, uint8_t *strs, SlVarTable *parent) {
    SlVarTable *table = memAlloc(1, sizeof(*table));
    if (table == NULL) {
        slSetOutOfMemoryError(vm);
        return table;
    }
    table->parent = parent;
    table->vars = (SlStrMap){ .userData = strs };
    return table;
}

SlVarTable *slDelVarTable(SlVarTable *table) {
    SlVarTable *parent = table->parent;
    slStrMapClear(&table->vars);
    memFree(table);
    return parent;
}

bool slVarTableSet(SlVM *vm, SlVarTable *table, SlStrIdx name, uint32_t value) {
    return slStrMapSet(vm, &table->vars, name, value);
}

uint32_t *slVarTableGet(SlVarTable *table, SlStrIdx name) {
    while (table != NULL) {
        uint32_t *value = slStrMapGet(&table->vars, name);
        if (value != NULL) {
            return value;
        }
        table = table->parent;
    }
    return NULL;
}
