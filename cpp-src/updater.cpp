// Thin C++20 CLI runner for InfinityOS Updater (non-Steel C++20 adapter rewritten by Gemini).
// Core logic is compiled from steel-core/*.steel into cpp-core/generated.cpp.
#define main __steel_unused_main
#include "../cpp-core/generated.cpp"
#undef main

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        steel::cli_args_store().emplace_back(argv[i]);
    }
    steel::Val res = steel::json_parse_val(steel_runner_cli_json());
    return static_cast<int>(steel::idx_get(res, steel::Val("exit_code")).as_num());
}
