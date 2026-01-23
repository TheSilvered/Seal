#include "seal.h"
#include <stdio.h>

#define slSource(str) (uint8_t *)(str), (sizeof(str) - 1)

int main(void) {
    SlVM vm = { 0 };
    SlSourceHandle src = slSourceFromCStr(&vm, "var hello = 4 + 4;");
    SlAst ast = slParse(&vm, src);
    if (vm.error.occurred) {
        printf("%s\n", vm.error.msg);
    }
    return 0;
}
