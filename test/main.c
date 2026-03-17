#if 1
#include "seal.h"
#include <stdio.h>
#include <stdlib.h>

#define slSource(str) (uint8_t *)(str), (sizeof(str) - 1)

void checkError(SlVM *vm);

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("USAGE: test <file.sl>\n");
        return 1;
    }
    SlVM vm = { 0 };
    SlSource *src = slSourceFromFile(&vm, argv[1]);
    checkError(&vm);
    SlObj mainFunc = slGenCode(&vm, src);
    checkError(&vm);
    SlObj result = slRun(&vm, mainFunc);
    checkError(&vm);

    return 0;
}

void checkError(SlVM *vm) {
    if (vm->error.occurred) {
        printf("%s\n", vm->error.msg);
        exit(1);
    }
}
#else

#include <stdio.h>
#include <string.h>
#include <limits.h>

#include "sl_hashmap.h"

#define MAX_CMD_LEN 99

bool keyEq(char *s1, char *s2, void *userData) {
    (void)userData;
    return strcmp(s1, s2) == 0;
}

uint32_t keyHash(char *key, void *userData) {
    (void)userData;
    return slFNVHash(key, strlen(key));
}

slHashMapType(char *, int, StrMap, strMap)
slHashMapImpl(char *, int, StrMap, strMap, keyEq, keyHash)

static char cmd[LINE_MAX];

char *input(const char *prompt) {
    printf("%s", prompt);
    fflush(stdout);
    fgets(cmd, sizeof(cmd), stdin);
    cmd[strlen(cmd) - 1] = '\0';
    return cmd;
}

int main(void) {
    StrMap map = { 0 };
    while (1) {
        char *cmd = input("> ");
        if (strcmp(cmd, "exit") == 0) {
            break;
        } else if (strcmp(cmd, "print") == 0) {
            slMapForeach(&map, StrMapBucket, b) {
                printf("%s: %d (%u)\n", b->key, b->value, b->hash & (map.cap - 1));
            }
        } else if (strcmp(cmd, "set") == 0) {
            char *keyStr = input("Key = ");
            char *key = memAllocBytes(strlen(keyStr) + 1);
            strcpy(key, keyStr);
            char *valueStr = input("Value = ");
            int value = atoi(valueStr);
            strMapSet(&map, key, value);
        } else if (strcmp(cmd, "get") == 0) {
            char *key = input("Key = ");
            int *value = strMapGet(&map, key);
            if (value == NULL) {
                printf("Key not found.\n");
            } else {
                printf("=> Value = %d\n", *value);
            }
        } else {
            printf("Invalid command '%s'. Write 'exit' to exit.\n", cmd);
        }
    }
    return 0;
}

#endif
