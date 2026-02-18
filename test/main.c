#include "seal.h"
#include <stdio.h>

#define slSource(str) (uint8_t *)(str), (sizeof(str) - 1)

int main(void) {
    SlVM vm = { 0 };
    SlSource src = slSourceFromCStr("var a = a + 3;");
    slGenCode(&vm, &src);
    if (vm.error.occurred) {
        printf("%s\n", vm.error.msg);
    }
    return 0;
}
