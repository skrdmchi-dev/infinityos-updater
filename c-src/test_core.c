/* Runs the Steel self-test suite compiled to C11. */
#define main __steel_unused_main
#include "../c-core/generated.c"
#undef main

int main(void) {
    SVal ok = steel_run_self_tests();
    int pass = sv_truthy(ok);
    sv_cleanup();
    if (!pass) {
        fprintf(stderr, "Steel C11 self-tests FAILED\n");
        return 1;
    }
    printf("Steel C11 self-tests PASSED\n");
    return 0;
}
