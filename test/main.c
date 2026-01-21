#include "seal.h"
#include <stdio.h>

#define slSource(str) (uint8_t *)(str), (sizeof(str) - 1)

void printTokens(SlTokens tokens) {
    for (uint32_t i = 0; i < tokens.tokenCount; i++) {
        SlToken tok = tokens.tokens[i];
        switch (tok.kind) {
        case SlToken_Plus:
            printf("+");
            break;
        case SlToken_Semicolon:
            printf(";");
            break;
        case SlToken_Equals:
            printf("=");
            break;
        case SlToken_NumInt:
            printf("int: %lld", tok.as.numInt);
            break;
        case SlToken_Ident:
            printf("ident: %.*s", tok.as.ident.len, tok.as.ident.value);
            break;
        case SlToken_KwVar:
            printf("var");
            break;
        case SlToken_KwFunc:
            printf("func");
            break;
        default:
            printf("???");
            break;
        }
        putchar('\n');
    }
}

int main(void) {
    SlVM vm = { 0 };
    SlTokens tokens = slTokenize(&vm, slSource("var a = 10;"));
    if (vm.error.occurred) {
        printf("%s\n", vm.error.msg);
    } else {
        printf("tokenCount = %zu\n", tokens.tokenCount);
        printTokens(tokens);
    }
    return 0;
}
