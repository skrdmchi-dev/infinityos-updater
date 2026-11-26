// Runs the Steel self-test suite compiled to C++20.
#define main __steel_unused_main
#include "../cpp-core/generated.cpp"
#undef main

int main() {
    steel::Val ok = steel_run_self_tests();
    if (!ok.truthy()) {
        std::cerr << "Steel C++20 self-tests FAILED\n";
        return 1;
    }
    std::cout << "Steel C++20 self-tests PASSED\n";
    return 0;
}
