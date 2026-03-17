#include "sl_hashmap.h"
#include <string.h>

uint32_t slFNVHash(const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;

    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }

    return hash;
}

static bool strMapEq(SlStrIdx str1, SlStrIdx str2, void *userData) {
    return str1.len == str2.len && memcmp(
        (uint8_t *)userData + str1.idx,
        (uint8_t *)userData + str2.idx,
        str1.len
    );
}

static uint32_t strMapHash(SlStrIdx str, void *userData) {
    return slFNVHash((uint8_t *)userData + str.idx, str.len);
}

slHashMapImpl(SlStrIdx, uint32_t, SlStrMap, slStrMap, strMapEq, strMapHash)
