#include "seal.h"
#include <stdio.h>

#define slSource(str) (uint8_t *)(str), (sizeof(str) - 1)

int main(void) {
    SlVM vm = { 0 };
    SlSource src = slSourceFromCStr("var a = 2 + 3; var b = a + 3;");
    SlObj mainFunc = slGenCode(&vm, &src);
    if (vm.error.occurred) {
        printf("%s\n", vm.error.msg);
        return 1;
    }
    SlObj result = slRun(&vm, mainFunc);
    if (vm.error.occurred) {
        printf("%s\n", vm.error.msg);
        return 1;
    } else {
        printf("%s\n", slTypeName(result));
    }
    return 0;
}
