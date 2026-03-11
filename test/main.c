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
