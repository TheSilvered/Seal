#include "seal.h"
#include <stdio.h>

#define slSource(str) (uint8_t *)(str), (sizeof(str) - 1)

void printTokens(SlTokens tokens) {
    for (uint32_t i = 0; i < tokens.tokenCount; i++) {
        SlToken tok = tokens.tokens[i];
        switch (tok.kind) {
        case SlToken_Add:
            printf("+\n");
            break;
        case SlToken_Int:
            printf("int: %lld\n", tok.as.intLiteral);
            break;
        case SlToken_Ident:
            printf("ident: %.*s\n", tok.as.str.len, tok.as.str.value);
            break;
        }
    }
}

int main(void) {
    SlVM vm = { 0 };
    SlTokens tokens = slTokenize(&vm, slSource("2 + 3"));
    if (vm.error.occurred) {
        printf("%s\n", vm.error.msg);
    } else {
        printf("tokenCount = %zu\n", tokens.tokenCount);
        printTokens(tokens);
    }
    return 0;
}
