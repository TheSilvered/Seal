#ifndef SL_HASHMAP_H_
#define SL_HASHMAP_H_

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#include "clib_mem.h"
#include "sl_lexer.h"

#define slHashMapType(KeyType, ValueType, Name, prefix)                        \
    typedef struct Name##Bucket {                                              \
        KeyType key;                                                           \
        ValueType value;                                                       \
        uint32_t hash;                                                         \
    } Name##Bucket;                                                            \
    typedef struct Name {                                                      \
        Name##Bucket *buckets;                                                 \
        void *userData; /* Passed to keyEq and keyHash functions */            \
        uint32_t len;                                                          \
        uint32_t cap; /* Power of two */                                       \
    } Name;                                                                    \
    bool prefix##Set(Name *map, KeyType key, ValueType value);                 \
    ValueType *prefix##Get(Name *map, KeyType key);                            \
    void prefix##Clear(Name *map);

// `bool keyEq(KeyType key1, KeyType key2, void *userData);`
// `uint32_t keyHash(KeyType key, void *userData);`
#define slHashMapImpl(KeyType, ValueType, Name, prefix, keyEq, keyHash)        \
    bool prefix##__grow(Name *map) {                                           \
        uint32_t newCap = map->cap ? map->cap * 2 : 16;                        \
        uint32_t mask = newCap - 1;                                            \
        Name##Bucket *newBuckets =                                             \
            memAllocZeroed(newCap, sizeof(Name##Bucket));                      \
        if (!newBuckets) {                                                     \
            return false;                                                      \
        }                                                                      \
        for (uint32_t i = 0; i < map->cap; i++) {                              \
            Name##Bucket *bucket = &map->buckets[i];                           \
            if (bucket->hash == 0) {                                           \
                continue;                                                      \
            }                                                                  \
            uint32_t idx = bucket->hash & mask;                                \
            while (newBuckets[idx].hash != 0) {                                \
                idx = (idx + 1) & mask;                                        \
            }                                                                  \
            newBuckets[idx] = *bucket;                                         \
        }                                                                      \
        memFree(map->buckets);                                                 \
        map->buckets = newBuckets;                                             \
        map->cap = newCap;                                                     \
        return true;                                                           \
    }                                                                          \
    bool prefix##Set(Name *map, KeyType key, ValueType value) {                \
        if (map->cap / 2 + map->cap / 4 < map->len + 1) {                      \
            if (!prefix##__grow(map)) {                                        \
                return false;                                                  \
            }                                                                  \
        }                                                                      \
        uint32_t hash = keyHash(key, map->userData);                           \
        if (hash == 0) {                                                       \
            hash = 1;                                                          \
        }                                                                      \
        uint32_t mask = map->cap - 1;                                          \
        for (uint32_t idx = hash & mask;; idx = (idx + 1) & mask) {            \
            Name##Bucket *bucket = &map->buckets[idx];                         \
            if (bucket->hash == 0) {                                           \
                bucket->hash = hash;                                           \
                bucket->key = key;                                             \
                bucket->value = value;                                         \
                map->len++;                                                    \
                return true;                                                   \
            }                                                                  \
            if (                                                               \
                bucket->hash == hash                                           \
                && keyEq(bucket->key, key, map->userData)                      \
            ) {                                                                \
                bucket->value = value;                                         \
                return true;                                                   \
            }                                                                  \
        }                                                                      \
        assert(false && "unreachable");                                        \
        return false;                                                          \
    }                                                                          \
    ValueType *prefix##Get(Name *map, KeyType key) {                           \
        if (map->cap == 0) {                                                   \
            return false;                                                      \
        }                                                                      \
        uint32_t hash = keyHash(key, map->userData);                           \
        if (hash == 0) {                                                       \
            hash = 1;                                                          \
        }                                                                      \
        uint32_t mask = map->cap - 1;                                          \
        for (uint32_t idx = hash & mask;; idx = (idx + 1) & mask) {            \
            Name##Bucket *bucket = &map->buckets[idx];                         \
            if (bucket->hash == 0) {                                           \
                return NULL;                                                   \
            }                                                                  \
            if (                                                               \
                bucket->hash == hash                                           \
                && keyEq(bucket->key, key, map->userData)                      \
            ) {                                                                \
                return &bucket->value;                                         \
            }                                                                  \
        }                                                                      \
        assert(false && "unreachable");                                        \
        return NULL;                                                           \
    }                                                                          \
    void prefix##Clear(Name *map) {                                            \
        memFree(map->buckets);                                                 \
        map->buckets = NULL;                                                   \
        map->len = 0;                                                          \
        map->cap = 0;                                                          \
    }

#define slMapForeach(map, BucketType, varName)                                 \
    for (uint32_t map__i = 0; map__i < (map)->cap; map__i++)                   \
        if ((map)->buckets[map__i].hash != 0)                                  \
            for (                                                              \
                BucketType *varName = &(map)->buckets[map__i];                 \
                varName;                                                       \
                varName = NULL                                                 \
            )

uint32_t slMemHash(const void *data, size_t len);

slHashMapType(SlStrIdx, uint32_t, SlStrMap, slStrMap)

#endif // !SL_HASHMAP_H_
