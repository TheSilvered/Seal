#include "sl_hashmap.h"

uint32_t slMemHash(const void *data, size_t len) {
    const uint8_t *bytes = data;

    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }

    return hash;
}

static bool strMapEq(SlStrIdx str1, SlStrIdx str2, void *userData) {
    return slStrIdxEq(str1, str2, (uint8_t *)userData);
}

static uint32_t strMapHash(SlStrIdx str, void *userData) {
    return slMemHash((uint8_t *)userData + str.idx, str.len);
}

slHashMapImpl(SlStrIdx, uint32_t, SlStrMap, slStrMap, strMapEq, strMapHash)
